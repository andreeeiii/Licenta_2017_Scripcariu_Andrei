#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <stdio.h>

#define AV_CODEC_FLAG_GLOBAL_HEADER   (1 << 22)


// Functie pentru tinerea evidentei pachetelor
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    printf("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
        tag,
        av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
        av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
        av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
        pkt->stream_index);
}

int main(int argc, char **argv)
{
    // Le facem NULL ca sa prevenim segmentation fault
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    AVOutputFormat *ofmt = NULL;
    AVPacket pkt;
    const char *in_filename;
    char *out_filename;
    int ret,i;
    int videoStreamIndex;

    if (argc < 3)
    {
        printf("usage: %s input_file output_file video_stream_index\n", argv[0]);
        return 1;
    }

    in_filename = argv[1];
    out_filename = argv[2];
    videoStreamIndex = atoi(argv[3]);

    // Inregistram formatele si codecurile
    av_register_all();

    // Incercam sa deschidem fisierul dat ca input si ii extragem format contextul
    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0)
    {
        fprintf(stderr, "Nu am putut deschide fisierul input '%s'", in_filename);
        goto end;
    }

    // Incercam sa extragem informatiile din stream
    if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0)
    {
        fprintf(stderr, "Failed to retrieve input stream information");
        goto end;
    }

    // Afisam informatiile pe ecran
    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    sprintf(out_filename, "video%d.mp4", videoStreamIndex);

    // Alocam output context pentru fisierul ce va fi creat
    avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, out_filename);
    if (!ofmt_ctx)
    {
        fprintf(stderr, "Could not create output context\n");
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    ofmt = ofmt_ctx->oformat;

    // Iteram prin toate streamurile din fisierul input
    for (i = 0;i < ifmt_ctx->nb_streams;i++)
    {
        AVStream *in_stream = ifmt_ctx->streams[i];

		// Adaugam un nou stream fisierului media (output)
        AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);

        if (!out_stream)
        {
            fprintf(stderr, "S-a intamplat o problema la alocarea streamului catre fisierul output\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

		// Copiem setarile streamului preluat catre streamul output
        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);

        if (ret < 0)
        {
            fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
            goto end;
        }

        out_stream->codec->codec_tag = 0;

		// Setam un flag pentru a reusi copierea
        if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        {
            out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
    }

    // Afisam informatiile ce le-am copiat catre fisierul output
    av_dump_format(ofmt_ctx, 0, out_filename, 1);

	// Deschidem fisierul cu drepturi de write
    if (!(ofmt->flags & AVFMT_NOFILE))
    {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            fprintf(stderr, "Nu am reusit sa deschidem fisierul output '%s'", out_filename);
            goto end;
        }
    }

    // Scriem antetul fisierului output
	// Prin setarea optiunii il facem pipe-ready
    AVDictionary *d = NULL;
    av_dict_set(&d, "frag_size", "1048576", 0);
    ret = avformat_write_header(ofmt_ctx, &d);
    if (ret < 0)
    {
        fprintf(stderr, "Error occured when opening output file\n");
        goto end;
    }

    // Citim cadru cu cadru din fisierul input sub forma de pachete si le copiem catre fisierul output
    while (1)
    {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
        {
            break;
        }
        if (pkt.stream_index == videoStreamIndex)
        {
            in_stream = ifmt_ctx->streams[pkt.stream_index];
            out_stream = ofmt_ctx->streams[pkt.stream_index];

            log_packet(ifmt_ctx, &pkt, "in");

            //Copierea pachetelor
            pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.pos = -1;
            log_packet(ofmt_ctx, &pkt, "out");

            // Scrierea de cadru intercorelat
            ret = av_interleaved_write_frame(ofmt_ctx, &pkt);
            if (ret < 0)
            {
                fprintf(stderr, "Error muxing packet\n");
                break;
            }
            av_packet_unref(&pkt);
        }
    }

    // Scriem stream trailer catre output
    av_write_trailer(ofmt_ctx);

    end:
	// Clean-up
    avformat_close_input(&ifmt_ctx);
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
    {
        avio_closep(&ofmt_ctx->pb);
    }
    avformat_free_context(ofmt_ctx);
    if (ret < 0 && ret != AVERROR_EOF)
    {
        fprintf(stderr, "A aparut eroarea: %s\n", av_err2str(ret));
        return 1;
    }

    return 0;
}