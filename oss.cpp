#include <iostream>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/wait.h>
#include <ctime>
#include <cstring>
#include <sys/shm.h>
#include <cstdlib>

#define MAX_PROCESSES 20
#define CLOCK_KEY 11111
#define MESSAGE_KEY 22222

struct PCB
{
    int occupied; // either true or false
    pid_t pid; // process id of this child
    int startSeconds; // time when it was created
    int startNano; // time when it was created
    int serviceTimeSeconds; // total seconds it has been "scheduled"
    int serviceTimeNano; // total nanoseconds it has been "scheduled"
    int eventWaitSec; // when does its event happen?
    int eventWaitNano; // when does its event happen?
    int blocked; // is this process waiting on event?
};

struct PCB processTable[MAX_PROCESSES];

struct Message
{
    long mtype;
    pid_t pid;
    int blocked;
};

struct Clock
{
    int seconds;
    int nanoseconds;
};

void forkWorker(int msgid, int shmid)
{
    pid_t pid = fork();
    if (pid == -1)
    {
        std::cerr << "Error forking process" << std::endl;
        exit(1);
    }
    else if (pid == 0)
    {
        // child process
        // attach to shared memory
        execl("./worker", "./worker", std::to_string(msgid).c_str(), std::to_string(shmid).c_str(), NULL);
        std::cerr << "Error executing worker" << std::endl;
        exit(1);
    }
    else
    {
        // parent process
        // find an empty spot in the process table
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if (processTable[i].occupied == 0)
            {
                processTable[i].occupied = 1;
                processTable[i].pid = pid;
                processTable[i].startSeconds = 0;
                processTable[i].startNano = 0;
                processTable[i].serviceTimeSeconds = 0;
                processTable[i].serviceTimeNano = 0;
                processTable[i].eventWaitSec = 0;
                processTable[i].eventWaitNano = 0;
                processTable[i].blocked = 0;

                std::cout << "Forked process with pid: " << pid << std::endl;
                break;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    // initialize process table
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        processTable[i].occupied = 0;
    }

    // initialize shared memory
    int shmid = shmget(CLOCK_KEY, sizeof(Clock), IPC_CREAT | 0666);
    if (shmid == -1)
    {
        std::cerr << "Error creating shared memory" << std::endl;
        return 1;
    }

    Clock *clock = (Clock *)shmat(shmid, NULL, 0);
    if (clock == (void *)-1)
    {
        std::cerr << "Error attaching to shared memory" << std::endl;
        return 1;
    }
    clock->seconds = 0;
    clock->nanoseconds = 0;

    // initialize message queue
    int msgid = msgget(MESSAGE_KEY, IPC_CREAT | 0666);
    if (msgid == -1)
    {
        std::cerr << "Error creating message queue" << std::endl;
        return 1;
    }

    // fork worker
    forkWorker(msgid, shmid);

    //send message to worker
    Message message;
    message.mtype = 1;
    message.pid = processTable[0].pid;
    message.blocked = 0;

    // send message to worker
    if (msgsnd(msgid, &message, sizeof(message), 0) == -1)
    {
        std::cerr << "Error sending message to worker" << std::endl;
        return 1;
    }
    else
    {
        std::cout << "Sent message to worker" << std::endl;
    }

    // wait for worker to send message back
    if (msgrcv(msgid, &message, sizeof(message), 1, 0) == -1)
    {
        std::cerr << "Error receiving message from worker" << std::endl;
        return 1;
    }
    else
    {
        std::cout << "Received message from worker" << std::endl;
    }

    // detach from shared memory
    shmdt(clock);

    // remove shared memory
    shmctl(shmid, IPC_RMID, NULL);

    // remove message queue
    msgctl(msgid, IPC_RMID, NULL);

    return 0;
}