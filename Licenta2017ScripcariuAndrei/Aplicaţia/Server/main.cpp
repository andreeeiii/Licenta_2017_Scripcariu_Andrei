#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <fstream>
#include <stdio.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>

using namespace std;

#define PORT 8554

extern int errno;

int main(int argc, char *argv[])
{
    // verificare argumentele date la linia de comanda
    if (argc != 3)
    {
        printf ("Sintaxa: %s <dimensiunea_pachetelor_trimise> <din_ce_ts_extrage>\n", argv[0]);
        return -1;
    }


    char *my_args[5]; // argumente pentru exec
    pid_t pid; // folosit pentru fork
    struct sockaddr_in server;	// structura folosita de server
    struct sockaddr_in client; // structura folosita de client
    int sd; // descriptorul de socket
    const int chunkSize = atoi(argv[1]);
    char dChunk[chunkSize];
    FILE* rFile; // fisierul din care citim

    // crearea unui socket
    if ((sd = socket (AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror ("[server]Eroare la socket().\n");
        return errno;
    }

    // pregatirea structurilor de date
    bzero(&server, sizeof(server));
    bzero(&client, sizeof(client));

    // umplem structura folosita de server
    // stabilirea familiei de socket-uri
    server.sin_family = AF_INET;
    // acceptam orice adresa
    server.sin_addr.s_addr = htonl (INADDR_ANY);
    // utilizam un port utilizator
    server.sin_port = htons (PORT);

    // atasam socketul
    if (bind (sd, (struct sockaddr *) &server, sizeof (struct sockaddr)) == -1)
    {
        perror ("[server]Eroare la bind().\n");
        return errno;
    }

      // servim in mod iterativ clientii
    while (1)
    {
        int recv;
        char fileName[100] = "video";
        char streamNo[2];
        int length = sizeof (client);
        printf ("[server]Astept la portul %d...\n",PORT);
        fflush (stdout);

        // S-a conectat un client
        char clientConnRecv[100];
        if ((recv = recvfrom(sd, clientConnRecv, 100, 0,(struct sockaddr*) &client, (socklen_t*)&length)) <= 0)
        {
            perror ("[server]Eroare la recvfrom() de la client.\n");
            return errno;
        }

        char information[100], infoFileName[100];
        sprintf(infoFileName, "information%s", argv[2]);
        FILE *pFile;
        pFile = fopen (infoFileName, "r");
        // citim din fisierul de informatii si trimitem clientului
        while (fgets(information, 100, pFile))
        {
            if (sendto(sd, information, 100, 0, (struct sockaddr*) &client, length) <= 0)
            {
                perror("[server] Eroare la sendto informatii catre client.\n");
                continue;
            }
        }
        // trimitem un string cunoscut de client pentru a iesi din recvfrom
        strcpy(information, "over");
        sendto(sd, information, 100, 0 , (struct sockaddr*)&client, length);
        fclose (pFile);

        bzero (dChunk, chunkSize);
        // aici primim stream indexul
        if ((recv = recvfrom(sd, streamNo, 2, 0,(struct sockaddr*) &client, (socklen_t*)&length)) <= 0)
        {
            perror ("[server]Eroare la recvfrom() de la client.\n");
            return errno;
        }
        else
        {
            if ( (pid = fork()) == 0)
            {
                // Procesul copil
                // Copiem argumentele primite de la server
                //char cFName[25] = "CreateFiles";
                //memcpy (my_args[0], cFName, 25);
                my_args[0] = "CreateFiles";
                my_args[1] = argv[2];
                //char vName[10] = "video";
                //memcpy (my_args[2], vName, 10);
                my_args[2] = "video";
                my_args[3] = streamNo;
                my_args[4] = nullptr;
                execv ("CreateFiles", my_args);
                puts("Uh oh! If this prints, execv() must have failed");
                exit(EXIT_FAILURE);
            }
            else
            {
                //Parent process
                waitpid(-1, NULL, 0);
                char chunkValue[100];
                sprintf(chunkValue,"%d", chunkSize);
                // Trimitem dimensiunea pachetelor clientului
                if (sendto(sd, chunkValue, 100, 0, (struct sockaddr*) &client, length) <= 0)
                {
                    perror ("[server]Eroare la sendto() la client.\n");
                    return errno;
                }

                int streamNumber = atoi(streamNo);
                sprintf(fileName, "video%d.mp4", streamNumber);
                char verify[100] = "lala";
                // Spunem copilului ca suntem pregatiti de transmisia datelor
                if (sendto(sd, verify, 100, 0, (struct sockaddr*) &client, length) <= 0)
                {
                    perror ("[server]Eroare la sendto() chunkSize catre client.\n");
                    continue;
                }
                else
                {
                    rFile = fopen(fileName, "rb");
                    // Transmitem pachete citite din fisierul media ales de client
                    while (fread(dChunk, sizeof(char), chunkSize, rFile) != 0)
                    {
                        // trimitem date catre client
                        if (sendto(sd, dChunk, chunkSize, 0, (struct sockaddr*) &client, length) <= 0)
                        {
                            perror ("[server]Eroare la sendto() catre client.\n");
                            continue;		// continuam sa ascultam
                        }
                        else
                        {
                            printf ("[server]S-a trimis cu succes un data chunk\n");
                            usleep(3000);
                        }
                    }
                }
             }
        fclose(rFile);
        }
    }
    return 0;
}
