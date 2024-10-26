#include <iostream>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <cstdlib>

#define CLOCK_KEY 11111
#define MESSAGE_KEY 22222

struct Message
{
    long mtype;
    pid_t pid;
    int blocked;
};

int main(int argc, char* argv[])
{
    int msgid = atoi(argv[1]);
    int shmid = atoi(argv[2]);

    //attach to clock
    int* clock = (int*)shmat(shmid, NULL, 0);
    if (clock == (void *)-1)
    {
        std::cerr << "Error attaching to shared memory" << std::endl;
        return 1;
    }

    //wait to receive OSS message
    Message message;
    if (msgrcv(msgid, &message, sizeof(message), getpid(), 0) == -1)
    {
        std::cerr << "Error: Failed to receive message from OSS" << std::endl;
        return 1;
    }
    else
    {
        std::cout << "User: " << getpid() << " received message from OSS" << std::endl;
    }

    //send message to OSS
    message.mtype = 1;
    message.pid = getpid();
    message.blocked = 0;
    if (msgsnd(msgid, &message, sizeof(message), 0) == -1)
    {
        std::cerr << "Error: Failed to send message to OSS" << std::endl;
        return 1;
    }
    else
    {
        std::cout << "User: " << getpid() << " sent message to OSS" << std::endl;
    }

    shmdt(clock);
}