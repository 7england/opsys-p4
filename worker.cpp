#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <ctime>

const int SH_KEY = 74821;
const int MSG_KEY = 49174;
const int PERMS = 0644;
const int BILLION = 1000000000;

struct Clock
{
    int seconds;
    int nanoseconds;
};

struct Message
{
    long msgtype; //type of msg
    pid_t pid; //pid of sender
    int time; //time slice/used in ns; if negative, worker is terminating
};

int simulateWork(int timeSlice)
{
    //simulate work
    //10% chance of terminating
    int terminateChance = rand() % 100;
    if (terminateChance < 10)
    {
        int usedTime = rand() % (timeSlice + 1); //use entire time slice
        return -usedTime; //terminate
    }
    //90% chance of working
    else
    {
        int blockChance = rand() % 100;
        if (blockChance < 50) //50% chance of blocking
        {
            int r = rand() % 6; //r in range [0, 5]
            int s = rand() % 1001; //s in range [0, 1000]
            int blockTime = r * BILLION + s; //block time in ns

            return blockTime;
        }
        else //50% chance of working
        {
            int p = rand() % 100; //p in range [1, 99]
            if (p < timeSlice)
            {
                return p;
            }
            else
            {
                return timeSlice;
            }
        }
    }
    //if no work done
    return -1;
}

int main()
{
    std::cout << "Worker " << getpid() << ": Starting" << std::endl;
    //https://stackoverflow.com/questions/55833470/accessing-key-t-generated-by-ipc-private
    int shmid = shmget(SH_KEY, sizeof(Clock), PERMS); //<-----
    if (shmid == -1)
    {
        std::cerr << "Worker " << getpid() <<": Error: Shared memory get failed" << std::endl;
        return 1;
    }

    //attach shared mem
    Clock *shared_clock = static_cast<Clock*>(shmat(shmid, nullptr, 0));
    if (shared_clock == (void*)-1)
    {
        std::cerr << "Worker: Error: shmat" << std::endl;
        return 1;
    }

    int msgid = msgget(MSG_KEY, PERMS);
    if (msgid == -1)
    {
        std::cerr << "Worker: Error: msgget failed" << std::endl;
        return 1;
    }

    //seed random number generator
    srand(static_cast<unsigned int>(getpid() + time(0)));

    while(true)
    {

        //get message from oss
        Message msg;

        if(msgrcv(msgid, &msg, sizeof(Message) - sizeof(long), getpid(), 0) == -1)
        {
            std::cerr << "Worker " << getpid() << ": Error: msgrcv failed" << std::endl;
            return 1;
        }
        //get timeslice from oss msg
        int timeSlice = msg.time;

        std::cout << "Worker " << getpid() << ": Received time slice " << timeSlice << " from OSS" << std::endl;
        //simulate work
        int workDone = simulateWork(timeSlice);

        Message response;
        response.msgtype = getppid();
        response.pid = getpid();
        response.time = workDone;

        //send message to oss
        if(msgsnd(msgid, &response, sizeof(Message) - sizeof(long), 0) == -1)
        {
            std::cerr << "Worker " << getpid() << ": Error: msgsnd failed" << std::endl;
            return 1;
        }

    }

    //detach shared mem
    if(shmdt(shared_clock) == -1)
    {
        std::cerr << "Worker " << getpid() << ": Error: shmdt failed" << std::endl;
        return 1;
    }

    return 0;
}