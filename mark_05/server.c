#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <time.h>

#include "../tools.h"

pot *shared_pot;
sem_t *sem_pot;
int shm_fd;
int servSock;

void clean() {
    close(shm_fd);
    sem_close(sem_pot);
    sem_unlink(SEM_POT);
    munmap(shared_pot, sizeof(pot));
    shm_unlink(POT);
    close(servSock);
}

void handle_exit() {
    clean();
    exit(0);
}

int AcceptTCPConnection(int childServSock) {
    int clntSock;
    struct sockaddr_in echoClntAddr;
    unsigned int clntLen;
    clntLen = sizeof(echoClntAddr);

    if ((clntSock = accept(childServSock, (struct sockaddr *) &echoClntAddr, &clntLen)) < 0) {
        exit_error("accept() failed");
    }
    printf("Handling client %s\n", inet_ntoa(echoClntAddr.sin_addr));

    return clntSock;
}

int CreateTCPServerSocket(unsigned short port) {
    int sock;
    struct sockaddr_in echoServAddr;

    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        exit_error("socket() failed");
    }

    memset(&echoServAddr, 0, sizeof(echoServAddr));
    echoServAddr.sin_family = AF_INET;
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    echoServAddr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0) {
        exit_error("bind() failed\n");
    }

    if (listen(sock, MAXPENDING) < 0) {
        exit_error("listen() failed\n");
    }

    return sock;
}

void HandleTCPClient(int clntSocket) {
    char rcvBuff[1];
    int recvMsgSize;
    char c;
    if ((recvMsgSize = recv(clntSocket, rcvBuff, 1, 0)) < 0)
        exit_error("recv() failed");
    if (recvMsgSize == 1) {
        if (rcvBuff[0] == BEE_IN) {
            sem_wait(sem_pot);
            if (shared_pot->current_honey < shared_pot->capacity) {
                ++shared_pot->current_honey;
                printf("Bee has added one honey. Total honey: %d\n", shared_pot->current_honey);
            }
            sem_post(sem_pot);
            c = BEE_IN;
            if (send(clntSocket, &c, 1, 0) != 1) {
                exit_error("send() bee failed\n");
            }
        } else if (rcvBuff[0] == BEE_OUT) {
            c = BEE_OUT;
            if (send(clntSocket, &c, 1, 0) != 1) {
                exit_error("send() bee failed\n");
            }
        } else if (rcvBuff[0] == BEAR) {
            sem_wait(sem_pot);
            if (shared_pot->current_honey == shared_pot->capacity) {
                printf("Bear ate all honey. Total honey taken: %d\n", shared_pot->current_honey);
                shared_pot->current_honey = 0;
                c = STOLE;
                if (send(clntSocket, &c, 1, 0) != 1) {
                    exit_error("send() failed\n");
                }
            } else {
                printf("Bear is sleeping right now! (pot is not full)\n");
                c = EMPTY;
                if (send(clntSocket, &c, 1, 0) != 1) {
                    exit_error("send() failed\n");
                }
            }
            sem_post(sem_pot);
        } else {
            printf("Wrong data\n");
        }
    }
}

void ProcessMain(int childServSock) {
    int clntSock = AcceptTCPConnection(childServSock);
    printf("with child process: %d\n", (unsigned int) getpid());
    while (1) {
        HandleTCPClient(clntSock);
    }
}

int main(int argc, char *argv[]) {
    prev_server = signal(SIGINT, handle_exit);
    srand(time(NULL));

    if ((shm_fd = shm_open(POT, O_CREAT | O_RDWR, 0666)) == -1) {
        exit_error("Smth wrong with shm_open call \n");
    }

    if (ftruncate(shm_fd, sizeof(pot)) == -1) {
        exit_error("Smth wrong with ftruncate call \n");
    }

    if ((shared_pot = mmap(NULL, sizeof(pot), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED) {
        exit_error("Smth wrong with mmap call \n");
    }

    if ((sem_pot = sem_open(SEM_POT, O_CREAT, 0666, 1)) == SEM_FAILED) {
        exit_error("Smth wrong with sem_open call \n");
    }

    if (argc != 4) {
        fprintf(stderr, "Usage:  %s <SERVER PORT> <BEES_NUMBER> <SIPS_NUMBER>\n", argv[0]);
        exit(1);
    }
    
    int bees = atoi(argv[2]);
    int sips = atoi(argv[3]);
    shared_pot->current_honey = 0;
    shared_pot->capacity = sips;
    sem_post(sem_pot);
    unsigned short echoServPort;
    pid_t processID;
    unsigned int processCt;


    echoServPort = atoi(argv[1]);

    servSock = CreateTCPServerSocket(echoServPort);
    for (processCt = 0; processCt < bees + 1; processCt++) {
        if ((processID = fork()) < 0) {
            exit_error("fork() failed\n");
        } else if (processID == 0) {
            signal(SIGINT, prev_server);
            ProcessMain(servSock);
            exit(0);
        }
    }
    while (1) {}
}
