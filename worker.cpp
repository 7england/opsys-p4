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

bool blocked = false;

struct Clock
{
    int seconds;
    int nanoseconds;
};

struct Message
{
    long msgtype; //type of msg
    pid_t pid; //pid of sender
    long long timeSlice; //time slice/used in ns; if negative, worker is terminating
    int blocked; //if worker is blocked
};

long long simulateWork(long long timeSlice)
{
    //simulate work
    //10% chance of terminating
    long long terminateChance = rand() % 100;
    long long chance = rand() % 100; //chance in range 1-100%

    if (terminateChance < 10)
    {
        long long usedTime = rand() % (timeSlice + 1); //use entire time slice
        return -usedTime; //terminate
    }
    //90% chance of working/blocking
    else if (chance > 50) //work done taking part of time and requesting I/O
    {
        //blocked
        //50% chance of blocking
        long long r = rand() % 6; //r in range [0, 5]
        long long s = rand() % 1001; //s in range [0, 1000]
        long long blockTime = r * BILLION + s; //run time before block in ns
        blocked = true;

        return blockTime;
    }
    else if (chance <= 50)
    {
        //not blocked
        //calculate p time taken, where p is a random number between [1,99]
        int p = rand() % 100 + 1; //p in range [1, 100]
        //return 1-100% of time slice
        long long workTime = timeSlice * (p / 100.0); //work time in ns
        std::cout << "Worker " << getpid() << ": Work done: " << workTime << " ns" << std::endl;

        return workTime;
    }
    //if no work done
    return -1;
}

int main()
{
    //https://stackoverflow.com/questions/55833470/accessing-key-t-generated-by-ipc-private
    int shmid = shmget(SH_KEY, sizeof(Clock), PERMS); //<-----
    if (shmid == -1)
    {
        std::cerr << "Worker " << getpid() <<": Error: Shared memory get failed" << std::endl;
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
        long long timeSlice = msg.timeSlice;

        //simulate work
        long long workDone = simulateWork(timeSlice);

        Message response;
        response.msgtype = getppid();
        response.pid = getpid();
        response.timeSlice = workDone;
        if(!blocked)
        {
            response.blocked = 0;
        }
        else if (blocked)
        {
            response.blocked = 1;
            blocked = false;
        }
        else
        {
            response.blocked = 0;
        }

        //send message to oss
        if(msgsnd(msgid, &response, sizeof(Message) - sizeof(long), 0) == -1)
        {
            std::cerr << "Worker " << getpid() << ": Error: msgsnd failed" << std::endl;
            return 1;
        }

    }

    return 0;
}