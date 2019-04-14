#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <mutex>
#include <queue>
#include <string.h>
#include <fstream>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>

using namespace std;

#define NELEMENTS 256

int chunkSize;
int fd1; // descriptorul de pipe
int verifNumber = 0;
extern int errno;     // codul de eroare returnat de anumite apeluri
int port;     // portul de conectare la server
int sd;     // descriptorul de socket
struct sockaddr_in server;     // structura folosita pentru conectare

void *writeToBuf(void *ptr);
void *readFromBuf(void *ptr);

pthread_mutex_t my_mutex = PTHREAD_MUTEX_INITIALIZER;

class MutexGuard
{
public:
        MutexGuard() { pthread_mutex_lock(&my_mutex); }
        ~MutexGuard() { pthread_mutex_unlock(&my_mutex); }
};

class Queue
{
public:
    char** buf;
    int read_ptr;
    int write_ptr;
    int counter; // numarul de elemente din buffer

    Queue()
    {
        counter = 0;
        read_ptr = 0;
        write_ptr = 0;
        buf = new char*[NELEMENTS];
    }

    ~Queue()
    {
        for (int i = 0; i < NELEMENTS; i++)
        {
            delete[] buf[i];
        }
        delete[] buf;

    }

    void push(char element[]) // scrie de la server in buffer
    {
        MutexGuard g_lock;
        if (verifNumber < NELEMENTS)
        {
            buf[write_ptr] = (char*)malloc(chunkSize);
        }
        memcpy (buf[write_ptr], element, chunkSize);
        counter++;
        write_ptr++;
        write_ptr = write_ptr % NELEMENTS;
        verifNumber++;
    }

    char* pop(char* element) // scoate din buffer elementul curent si il scrie catre pipe
    {
        MutexGuard g_lock;
        int result;
        memcpy (element, buf[read_ptr], chunkSize);
        counter--;
        read_ptr++;
        read_ptr = read_ptr % NELEMENTS;
        if (strcmp(element,"s_a_terminat_boss") != 0)
        {
            write (fd1, element, chunkSize); // scrie catre named pipe
        }
        return element;
    }

    bool isFull() // verifica daca numarul de elemente din buffer e full
    {
        MutexGuard g_lock;
        if (counter == NELEMENTS)
            return true;
        else
            return false;

    }

    bool isEmpty() // verifica daca numarul de elemente din buffer e gol
    {
        MutexGuard g_lock;
        if (counter == 0)
            return true;
        else
            return false;
    }

} fifo;

int main(int argc, char *argv[])
{
    int rc1, rc2, result;
    pthread_t readT, writeT;

    // verificare argumentele date la linia de comanda
    if (argc != 3)
    {
        printf ("Sintaxa: %s <adresa_server> <port> \n", argv[0]);
        return -1;
    }

    fprintf(stdout, "Open pipe\n");

    fd1 = open("/tmp/mypipe", O_WRONLY); // deschide cu drepturi de write named pipe-ul
    if (fd1 < 0)
    {
        perror("Could not open mypipe\n");
        exit(-1);
    }

    // stabilim portul
    port = atoi (argv[2]);

    // cream socketul
    if ((sd = socket (AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        perror ("Eroare la socket().\n");
        return errno;
    }

    // umplem structura folosita pentru realizarea dialogului cu serverul
    // familia socket-ului
    server.sin_family = AF_INET;
    // adresa IP a serverului
    server.sin_addr.s_addr = inet_addr(argv[1]);
    // portul de conectare
    server.sin_port = htons (port);

    fflush (stdout);

    long length = sizeof(server);

    // trimitem catre server un mesaj pentru a-i confirma ca ne-am conectat
    char clientConn[100] = "AttemptingConnection";
    if (sendto(sd, clientConn, 100, 0, (struct sockaddr *)&server, length) <= 0)
    {
        perror ("[server]Eroare la sendto() catre server.\n");
        exit(-1);
    }

    char information[100];
    // primim infomatiile legate de fisierul .ts de la server si le afisam
    while ( ((recvfrom(sd, information, 100, 0, (struct sockaddr *)&server, (socklen_t*)&length)) > 0) )
    {
        if (strcmp(information, "over") == 0)
        {
            break;
        }
        fprintf(stdout, "%s", information);
    }
    char streamNo[2];
    fprintf(stdout, "Alegeti-va unul din streamurile valabile pentru difuzare: \n");
    scanf(" %s", &streamNo);
    while (!(streamNo[0] >= '0' && streamNo[0] <= '9'))
    {
        printf("Caracterul introdus nu este un numar, alegeti altul: \n");
        scanf(" %s", &streamNo);
    }
    // trimite la server numele fisierlui pe care vrea clientul sa il vada
    if (sendto(sd, streamNo, 2, 0, (struct sockaddr *)&server, length) <= 0)
    {
        perror ("[server]Eroare la sendto() catre server.\n");
        exit(-1);
    }

    // aici primim sizeul pachetelor
    char chunkValue[100];
    if (recvfrom(sd, chunkValue, 100, 0, (struct sockaddr *)&server, (socklen_t *)&length) <= 0)
    {
        perror ("[server]Eroare la recvfrom() de la server.\n");
        exit(-1);
    }
    chunkSize = atoi(chunkValue);

    char verify[100];
    // Am intrat pe ramura parinte in server, putem porni threadurile
    if (recvfrom(sd, verify, 100, 0, (struct sockaddr *)&server, (socklen_t *)&length) <= 0)
    {
        perror ("[server]Eroare la recvfrom() de la server.\n");
        exit(-1);
    }
    else
    {
        // Facem recvfrom sa fie non-blocant
        int errCode;
        struct timeval read_timeout;
        read_timeout.tv_sec = 0;
        read_timeout.tv_usec = 10000;
        errCode = setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
        if (errCode < 0)
        {
            perror ("Error at set socket option\n");
            exit(-1);
        }

        // Creez primul thread, care ia datele primite de la server si scrie in buffer
        rc1 = pthread_create(&writeT, NULL, &writeToBuf, (void*)&length);
        if (rc1)
        {
            perror ("Unable to create write thread\n");
            exit(-1);
        }

        // Creez al doilea thread care ia din buffer si scrie catre named pipe
        rc2 = pthread_create(&readT, NULL, &readFromBuf, NULL);
        if (rc2)
        {
            perror ("Unable to create read thread\n");
            exit(-1);
        }

        // Astept ca ambele thread-uri sa se termine inainte sa se termine thread-ul main
        pthread_join(writeT, NULL);
        pthread_join(readT, NULL);

        // inchidem pipe
        close (fd1);
        // inchidem conexiunea
        close (sd);

    }
    return 0;
}

void *writeToBuf(void *ptr)
{
    char data[chunkSize];
    while ( ((recvfrom(sd, data, chunkSize, 0, (struct sockaddr *)&server, (socklen_t*)ptr)) > 0) )
    {
        bool my_value = fifo.isFull();
        if (my_value == true)
        {
            usleep(5000);
        }
        fifo.push(data);
        usleep(3000);
    }
     // trimitem un string ca sa stim ca s-a terminat threadul care scrie in buffer
    char off[chunkSize] = "s_a_terminat_boss";
    fifo.push(off);
}

void *readFromBuf(void *ptr)
{
    char element[chunkSize];
    while(1)
    {
        bool my_value = fifo.isEmpty();
        if (my_value == true)
        {
            usleep(5000);
        }
        fifo.pop(element);
        char *pch;
        if ( (pch = (strstr(element,"s_a_terminat_boss"))) != nullptr)
        {
            break;
        }
        usleep(3000);
    }
}
