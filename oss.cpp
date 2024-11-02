#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <iomanip>
#include <ctime>
#include <cstdlib>
#include <fstream>
#include <sys/msg.h>

//https://forum.arduino.cc/t/when-to-use-const-int-int-or-define/668071
const int PERMS = 0644;
const int SH_KEY = 74821;
const int MSG_KEY = 49174;
const int BILLION = 1000000000;
const int MAX_PROCESSES = 20;

std::string logFile = "logfile";

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

PCB pcb_table[MAX_PROCESSES];

struct Clock
{
    int seconds;
    int nanoseconds;
};

//https://stackoverflow.com/questions/41988823/is-this-how-message-queues-are-supposed-to-work
struct Message
{
    long msgtype; //type of msg
    pid_t pid; //pid of sender
    int action; //0 for terminate 1 for run
};

void signal_handler(int sig)
{
    std::cerr << "Timeout... terminating..." << std::endl;
    // code to send kill signal to all children based on their PIDs in process table
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (pcb_table[i].occupied == 1)
        {
            kill(pcb_table[i].pid, SIGKILL);
        }
    }

    // code to free up shared memory
    int shmid = shmget(SH_KEY, sizeof(Clock), 0);
    if (shmid != -1)
    {
        shmctl(shmid, IPC_RMID, nullptr);
    }

    msgctl(MSG_KEY, IPC_RMID, nullptr);
    exit(1);
}

void increment_clock(Clock *shared_clock, int activeChildren)
{
    if (activeChildren > 0)
    {
        shared_clock -> nanoseconds += (250000000 / activeChildren);
    }
    else
    {
        //do not increment
    }

    //increment seconds if nanoseconds = second
    if (shared_clock -> nanoseconds >= BILLION)
    {
        shared_clock -> nanoseconds -= BILLION;
        shared_clock -> seconds++;
    }
}

void output_to_log(const std::string &message)
{
    std::ofstream logFileStream(logFile, std::ios::app);
    if (logFileStream)
    {
        logFileStream << message << std::endl;
        std::cout << message << std::endl;
    }
    else
    {
        std::cerr << "Error: unable to open file." << std::endl;
    }
}

void print_process_table(PCB pcb_table[], Clock* shared_clock)
{
    std::cout << " SysClockS: " << shared_clock -> seconds <<
    " SysCLockNano: " << shared_clock -> nanoseconds <<
    "\nProcess Table:" <<
    "\n--------------------------------------------------------" << std::endl;
    std::cout << std::setw(10) << "Entry" <<
    std::setw(10) << "Occupied" <<
    std::setw(10) << "PID" <<
    std::setw(10) << "StartS" <<
    std::setw(10) << "StartN" <<
    std::endl;

    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        std::cout << std::setw(10) << i <<
        std::setw(10) << pcb_table[i].occupied <<
        std::setw(10) << pcb_table[i].pid <<
        std::setw(10) << pcb_table[i].startSeconds <<
        std::setw(10) << pcb_table[i].startNano <<
        std::endl;
    }
    std::cout << "--------------------------------------------------------" << std::endl;
}

void remove_from_PCB(pid_t dead_pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (pcb_table[i].pid == dead_pid)
        {
            pcb_table[i].occupied = 0;
            pcb_table[i].pid = 0;
            pcb_table[i].startSeconds = 0;
            pcb_table[i].startNano = 0;
            pcb_table[i].serviceTimeSeconds = 0;
            pcb_table[i].serviceTimeNano = 0;
            pcb_table[i].eventWaitSec = 0;
            pcb_table[i].eventWaitNano = 0;
            pcb_table[i].blocked = 0;
            break;
        }
    }
}

pid_t calculateNextChildToSendAMessageTo(pid_t lastChildMessaged)
{
    //find the index of the last child messaged
    int lastChildIndex = -1;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (pcb_table[i].occupied && pcb_table[i].pid == lastChildMessaged)
        {
            lastChildIndex = i;
            break;
        }
    }

    //start searching from the next index
    for (int i = lastChildIndex + 1; i < MAX_PROCESSES; i++)
    {
        if (pcb_table[i].occupied)
        {
            std::cout << "Returning pid: " << pcb_table[i].pid << std::endl;
            return pcb_table[i].pid;
        }
    }

    //no child was found, start searching from the beginning
    for (int i = 0; i <= lastChildIndex; i++)
    {
        if (pcb_table[i].occupied)
        {
            std::cout << "Returning pid: " << pcb_table[i].pid << std::endl;
            return pcb_table[i].pid;
        }
    }

    //no child is found, return -1
    return -1;
}

bool stillChildrenToLaunch(int launchedChildren, int numChildren)
{
    /*std::cout << "Launched children: " << launchedChildren << std::endl;
    std::cout << "Num children: " << numChildren << std::endl;
    std::cout << "Still children to launch: " << (launchedChildren < numChildren) << std::endl;*/
    return launchedChildren < numChildren;
}

bool stillChildrenRunning(int activeChildren)
{
    /*std::cout << "Active children: " << activeChildren << std::endl;*/
    return activeChildren > 0;
}

bool timePassed(long long sec1, long long nano1, long long sec2, long long nano2)
{
    return sec1 > sec2 || (sec1 == sec2 && nano1 >= nano2);
}

/*void schedule_process(Clock *shared_clock, int msgid, PCB pcb_table[])
{
    //schedule process
    int selected_index = -1;
    double min_ratio = 1.0;

    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (pcb_table[i].occupied == 0)
        {
            double total_time = (shared_clock -> seconds - pcb_table[i].startSeconds) + (shared_clock -> nanoseconds - pcb_table[i].startNano)/BILLION;
            double service_time = (pcb_table[i].serviceTimeSeconds) + (pcb_table[i].serviceTimeNano)/BILLION;
            double ratio = (total_time > 0) ? service_time/total_time : 0; //prevent divide by 0

            if (ratio < 1.0)
            {
                min_ratio = ratio;
                selected_index = i;
                break;
            }
        }

        if (selected_index != -1)
        {
            //if no process is selected, select the first one
            Message msg;
            msg.msgtype = pcb_table[selected_index].pid;
            msg.pid = getpid();
            msg.action = 1;

            increment_clock(shared_clock, 1);

            if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
            {
                std::cerr << "Error: msgsnd failed" << std::endl;
                return;
            }
            else
            {
                std::string logMessage = "Message sent to child " + std::to_string(pcb_table[selected_index].pid) + " at time " +
                    std::to_string(shared_clock->seconds) + "." + std::to_string(shared_clock->nanoseconds) + ".";
                output_to_log(logMessage);
            }
        }
    }
}*/

int main(int argc, char* argv[])
{
    //set up alarm
    signal(SIGALRM, signal_handler);
    alarm(60);

    //initialize variables for getopt
    int opt;
    int numChildren = 1;
    int numSim = 1;
    int timeLimSec = 1;
    int intervalMs = 100;

    while((opt = getopt(argc, argv, ":hn:s:t:i:f:")) != -1) //set optional args
        {
            switch(opt)
            {
                //help menu
                case 'h':
                    std::cout << "Help menu:\n" ;
                    std::cout << "**********************\n" ;
                    std::cout << "-h: display help menu\n" ;
                    std::cout << "-n: set number of child processes\n" ;
                    std::cout << "-s: set number of simultaneous children\n" ;
                    std::cout << "-t: set time limit for children in seconds\n" ;
                    std::cout << "-i: set interval in ms between launching children\n" ;
		    std::cout << "-f: choose file for oss output\n" ;
                    std::cout << "**********************\n" ;
                    std::cout << "Example invocation: \n" ;
                    std::cout << "./oss -n 5 -s 3 -t 7 -i 100\n" ;
                    std::cout << "Example will launch 5 child processes, with time limit between 1s and 7s,";
                    std::cout << "\nwith a time delay between new children of 100 ms\n" ;
                    std::cout << "\nand never allow more than 3 child processes to run simultaneously.\n" ;
                    return 0;
                case 'n':
                    numChildren = atoi(optarg); //assign arg value to numChildren
                    break;
                case 's':
                    numSim = atoi(optarg); //assign arg value to numSim
                    break;
                case 't':
                    timeLimSec = atoi(optarg);
                    break;
                case 'i':
                    intervalMs = atoi(optarg);
                    break;
		case 'f':
		    logFile = optarg;
		    break;
                default:
                    std::cerr << "Please choose an option!\n" ;
                    std::cout << "Example invocation: \n" ;
                    std::cout << "./oss -n 5 -s 3 -t 7 -i 100\n" ;
                    return 1;
            }
        }

        if (numChildren <= 0 || numSim <= 0 || timeLimSec <=0 || intervalMs <= 0)
        {
            std::cerr << "Please choose a valid number greater than 0." << std::endl;
            return 1;
        }
        if (numChildren > 20 || numSim > 20 || timeLimSec >= 60 || intervalMs >= 60000)
        {
            std::cerr << "Please choose a reasonable number. Max time: 60." << std::endl;
            return 1;
        }

    int shmid = shmget(SH_KEY, sizeof(Clock), IPC_CREAT | PERMS);
    if (shmid == -1)
    {
        std::cerr << "Error: Shared memory get failed" << std::endl;
        return 1;
    }

    Clock *shared_clock = static_cast<Clock*>(shmat(shmid, nullptr, 0));
    if (shared_clock == (void*)-1)
    {
        std::cerr << "Error: shmat" << std::endl;
        return 1;
    }

    shared_clock->seconds = 0;
    shared_clock->nanoseconds = 0;

    int msgid = msgget(MSG_KEY, IPC_CREAT | PERMS);
    if (msgid == -1)
    {
        std::cerr << "Error: msgget failed" << std::endl;
        return 1;
    }

    int launchedChildren = 0;
    int activeChildren = 0;
    pid_t lastChildMessaged = -1;

    long long nextLaunchTimeSec = 0;
    long long nextLaunchTimeNs = 0;
    long long nextPrintTimeSec = 0;
    long long nextPrintTimeNs = 500000;

    while (stillChildrenToLaunch(launchedChildren, numChildren) || stillChildrenRunning(activeChildren))
    {
        increment_clock(shared_clock, activeChildren);

        //if 50 ms passed print pcb
        if (timePassed(shared_clock->seconds, shared_clock->nanoseconds, nextPrintTimeSec, nextPrintTimeNs))
        {
            print_process_table(pcb_table, shared_clock);
            nextPrintTimeSec = shared_clock->seconds;
            nextPrintTimeNs = shared_clock->nanoseconds + 500000;

            //if nanoseconds > 1 billion, increment seconds
            if (nextPrintTimeNs >= BILLION)
            {
                nextPrintTimeNs -= BILLION;
                nextPrintTimeSec++;
            }
        }

        //check next child to send a message to using func
        pid_t nextChild = calculateNextChildToSendAMessageTo(lastChildMessaged);
        //std::cout << "Next child to send a message to: " << nextChild << std::endl;

        if (nextChild != -1)
        {
            //send msg to child to run
            Message msg;
            msg.msgtype = nextChild; //child pid
            msg.pid = getpid(); //parent pid
            msg.action = 1; //running, but doesn't really matter

            //send message of type nextChild pid for child to receive, visible with ipcs
            if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
            {
                std::cerr << "Error: msgsnd failed" << std::endl;
                return 1;
            }
            else
            {
                //save msg in log
                std::string logMessage = "Message sent to child " + std::to_string(nextChild) + " at time " +
                    std::to_string(shared_clock->seconds) + "." + std::to_string(shared_clock->nanoseconds) + ".";
                std::cout << "Message type " << msg.msgtype << std::endl;
                output_to_log(logMessage);
                //std::cout << "Sent" << std::endl;
            }

            lastChildMessaged = nextChild;  //update last messaged child
            std::cout << "Last child messaged: " << lastChildMessaged << std::endl;
            std::cout << "Next child to rcv msg from: " << nextChild << std::endl;

            long lastChildLong = static_cast<long>(lastChildMessaged);
            Message rcvMsg;
            if (msgrcv(msgid, &rcvMsg, sizeof(rcvMsg) - sizeof(long), lastChildLong, 0) != -1)
            {
                std::string logMessage = "Message received from child " + std::to_string(rcvMsg.pid) + " at time " +
                    std::to_string(shared_clock->seconds) + "." + std::to_string(shared_clock->nanoseconds) + ".";
                output_to_log(logMessage);
                std::cout << "Last child messaged: " << lastChildMessaged << std::endl;

                std::cout << "Msg Action: " << rcvMsg.action << std::endl;
                //check if child will terminate
                if (rcvMsg.action == 0)
                {
                    std::string logMessage = "Child " + std::to_string(rcvMsg.pid) + " terminated at time " +
                    std::to_string(shared_clock->seconds) + "." + std::to_string(shared_clock->nanoseconds);
                    output_to_log(logMessage);

                    waitpid(rcvMsg.pid, nullptr, 0);
                    remove_from_PCB(rcvMsg.pid);
                    activeChildren--;
                }
            }
            else
            {
                std::cerr << "OSS: Error: msgrcv failed" << std::endl;
                return 1;
            }
        }

        if ((activeChildren < numSim && launchedChildren < numChildren) &&
            ((shared_clock->seconds > nextLaunchTimeSec) ||
            (shared_clock->seconds == nextLaunchTimeSec && shared_clock->nanoseconds >= nextLaunchTimeNs)))
        {
            for (int i = 0; i < numSim; i++)
            {
                if (!pcb_table[i].occupied)
                {
                    pid_t new_pid = fork();

                    if (new_pid < 0)
                    {
                        //fork failed
                        std::cerr << "Error: fork issue." << std::endl;
                        exit(1);
                    }
                    else if (new_pid == 0)
                    {
                        //child process
                        int randomSec = rand() % timeLimSec + 1;
                        int randomNano = rand() % BILLION;

                        std::string randomSecStr = std::to_string(randomSec);
                        std::string randomNanoStr = std::to_string(randomNano);

                        execl("./worker", "worker", randomSecStr.c_str(), randomNanoStr.c_str(), nullptr);
                        std::cerr << "Error: execl failed" << std::endl;
                        exit(1);
                    }
                    else
                    {
                        //parent process
                        pcb_table[i].occupied = 1;
                        pcb_table[i].pid = new_pid;
                        pcb_table[i].startSeconds = shared_clock -> seconds;
                        pcb_table[i].startNano = shared_clock -> nanoseconds;
                        pcb_table[i].serviceTimeSeconds = 0;
                        pcb_table[i].serviceTimeNano = 0;
                        pcb_table[i].eventWaitSec = 0;
                        pcb_table[i].eventWaitNano = 0;
                        pcb_table[i].blocked = 0;
                        activeChildren++;
                        launchedChildren++;
                        break;
                    }
                }
            }
        }
    }
    //clean up
    shmdt(shared_clock);
    shmctl(shmid, IPC_RMID, nullptr);
    shmctl(msgid, IPC_RMID, nullptr);

    return 0;
}