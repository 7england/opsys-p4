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

const int SH_KEY = 74821;
const int MSG_KEY = 49174;
const int PERMS = 0644;
const int BILLION = 1000000000;

struct Clock
{
    int seconds;
    int nanoseconds;
};

/* herein lied the issue. RIP bug that took 3 hours to find that
was literally just me having msgtype as long and int in oss and
worker respectively. why did i not check the message struct properly
is a question that i wish i could ask my past self but alas
strange things happen to the human brain off of 2 hours of sleep
and 5 McDonalds coffees */
struct Message
{
    long msgtype; //type of msg
    pid_t pid; //pid of sender
    int action; //0 for terminate 1 for run
};

int main(int argc, char *argv[])
{
    int maxSec = std::atoi(argv[1]);
    int maxNsec = std::atoi(argv[2]);

    //https://stackoverflow.com/questions/55833470/accessing-key-t-generated-by-ipc-private
    int shmid = shmget(SH_KEY, sizeof(Clock), PERMS); //<-----
    if (shmid == -1)
    {
        std::cerr << "Worker: Error: Shared memory get failed" << std::endl;
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

    int termSec = shared_clock -> seconds + maxSec;
    int termNsec = shared_clock -> nanoseconds + maxNsec;

    if (termNsec >= BILLION)
    {
        termSec += termNsec / BILLION;
        termNsec = termNsec % BILLION;
    }

    std::cout << "\n\nWorker PID: " << getpid() << " PPID: " << getppid() <<
    " SysClockS: " << shared_clock -> seconds <<  " SysClockNano: " << shared_clock -> nanoseconds <<
    " TermTimeS: " << termSec << " TermTimeNano: " << termNsec <<
    "\n Starting.......\n\n" << std::endl;

    sleep(1);
    //message struct to receive messages
    Message rcvMsg;
    int iterationCount = 0; //count of iterations

    //do while loop from proj specs
    do
    {
        //get pid of worker
        pid_t pid = getpid();

        //receive message from oss with pid as msgtype
        if (msgrcv(msgid, &rcvMsg, sizeof(rcvMsg) - sizeof(long), pid, 0) == -1) //stuck on this line! <-----
        {
            std::cerr << "Worker " << pid << ": Error: msgrcv failed" << std::endl;
            return 1;
        }
        else
        {
            std::cout << "Worker: " << getpid() << " received message from oss" << std::endl;
        }

        //increment iteration count
        iterationCount++;

        Message msg;
        //always send a message back to the parent process after receiving a message
        msg.msgtype = getppid(); //pid of receiver
        msg.pid = getpid(); //pid of worker
        msg.action = 1; //run

        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
        {
            std::cerr << "Worker: Error: msgsnd failed" << std::endl;
            return 1;
        }
        else
        {
            std::cout << "Worker: " << getpid() << " sent message back to oss" << std::endl;
        }

        //check if we're out of time (reversed from other project to break if opp true
        if (shared_clock -> seconds > termSec ||
        (shared_clock -> seconds >= termSec && shared_clock -> nanoseconds >= termNsec))
        {
            //print info again
            std::cout << "\n\nWorker PID: " << getpid() << " PPID: " << getppid() <<
            " SysClockS: " << shared_clock -> seconds <<  " SysClockNano: " << shared_clock -> nanoseconds <<
            " TermTimeS: " << termSec << " TermTimeNano: " << termNsec << std::endl;

            //send message back to oss
            msg.msgtype = getppid();
            msg.pid = getpid();
            msg.action = 0; //terminate
            std::cout << "Message pid of " << msg.pid << " and action is " << msg.action << "\n\n" << std::endl;
            if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) ==-1)
            {
                std::cerr << "Worker: Error: msgsnd failed" << std::endl;
                return 1;
            }
            else
            {
                std::cout << "Worker " << msg.pid << ": Terminating after sending message back to oss after " << iterationCount << " iteration(s) has/have passed" << std::endl;
            }

            //determine if it is time to terminate
            break;
        }

        std::cout << "\n\nWorker PID: " << getpid() << " PPID: " << getppid() <<
        " SysClockS: " << shared_clock -> seconds <<  " SysClockNano: " << shared_clock -> nanoseconds <<
        " TermTimeS: " << termSec << " TermTimeNano: " << termNsec << std::endl;
        std::cout << "--" << iterationCount << " iteration(s) has/have passed since starting" << std::endl;

    } while (true);

    //clean up
    if (shmdt(shared_clock) == -1)
    {
        std::cerr << "Worker: error: shmdt" << std::endl;
        return 1;
    }

    shmctl (shmid, IPC_RMID, 0);
    //msgctl (msgid, IPC_RMID, 0);

    return 0;
}
