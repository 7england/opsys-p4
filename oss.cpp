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
#include <vector>
#include <queue>

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

//initialize ready and blocked queues
std::queue<PCB> readyQueue;
std::queue<PCB> blockedQueue;

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
    long long timeSlice; //time slice for process
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

void increment_clock(Clock *shared_clock, int timeUsedByOSSOrChild)
{
    shared_clock -> nanoseconds += timeUsedByOSSOrChild;

    //increment seconds if nanoseconds = second
    if (shared_clock -> nanoseconds >= BILLION)
    {
        shared_clock -> nanoseconds -= BILLION;
        shared_clock -> seconds++;
    }
    else
    {
        //do not increment
    }
}

void output_to_log(const std::string &message)
{
    static int totalLines = 0;
    const int MAX_LINES = 1000;
    static int fileIndex = 1;

    std::ofstream logFileStream;

    //check if total lines exceed 1000
    while(totalLines >= MAX_LINES)
    {
        totalLines = 0;
        //if more than 1000, add 1 to fileIndex
        fileIndex++;
    }

    //create file name with index incrementing if exceeding 1000 lines per file
    std::string fileName = logFile + std::to_string(fileIndex) + ".txt";

    //open file
    logFileStream.open(fileName, std::ios::app);

    //check if file is open
    if (!logFileStream)
    {
        std::cerr << "Error: Could not open file." << std::endl;
        return;
    }

    logFileStream << message << std::endl; //write message to file
    totalLines++; //increment line count

    logFileStream.close(); //close file
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
    std::cout << "PID to delete: " << dead_pid << std::endl;
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (pcb_table[i].pid == dead_pid)
        {
            std::cout << "Deleting pid: " << pcb_table[i].pid << std::endl;
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

void schedule_process(Clock *shared_clock, int msgid, PCB pcb_table[])
{
    std::cout << "Scheduling process..." << std::endl;
    //find the highest priority process
    int highestPriority = -1;
    int highestPriorityIndex = -1;

    //iterate through ready queue and check priorities
    std::queue<PCB> tempQueue;
    while (!readyQueue.empty())
    {
        std::cout << "Checking ready queue..." << std::endl;
        PCB temp = readyQueue.front(); //get front of queue
        readyQueue.pop(); //pop front of queue
        int priority = temp.serviceTimeSeconds + temp.serviceTimeNano;
        if (priority > highestPriority)
        {
            std::cout << "Highest priority: " << priority << std::endl;
            highestPriority = priority;
            highestPriorityIndex = temp.pid;
        }
        tempQueue.push(temp); //push temp to tempQueue
    }

    if (highestPriorityIndex != -1)
    {
        //send message to process
        std::cout << "Sending message to process..." << std::endl;
        Message msg;
        msg.msgtype = pcb_table[highestPriorityIndex].pid;
        msg.pid = pcb_table[highestPriorityIndex].pid;
        std::cout << "Msg pid: " << msg.pid << std::endl;
        msg.timeSlice = 5000;
        msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0);
        std::cout << "Message sent to process." << std::endl;
    }
    else
    {
        //no processes in ready queue, return
        return;
    }
}

void check_blocked_queue(Clock *shared_clock, PCB pcb_table[])
{
    std::cout << "Checking blocked queue..." << std::endl;
    //iterate through blocked queue and check if it's time to unblock
    std::queue<PCB> tempQueue;
    while (!blockedQueue.empty())
    {
        PCB temp = blockedQueue.front(); //get front of queue
        blockedQueue.pop(); //pop front of queue
        //check if it's time to unblock
        if (timePassed(shared_clock->seconds, shared_clock->nanoseconds, temp.eventWaitSec, temp.eventWaitNano))
        {
            //unblock process
            temp.blocked = 0;
            readyQueue.push(temp);

            std::string logOutput = "Child " + std::to_string(temp.pid) + " is unblocked at time " +
                std::to_string(shared_clock->seconds) + " s " + std::to_string(shared_clock->nanoseconds) + " ns.";
        }
        else
        {
            tempQueue.push(temp); //push temp to tempQueue
        }
    }
}

void remove_from_ready(pid_t stopped_pid, Clock* shared_clock)
{
    std::queue<PCB> tempQueue;
    while (!readyQueue.empty())
    {
        PCB temp = readyQueue.front(); //get front of queue
        readyQueue.pop(); //pop front of queue
        //check if pid is not equal to message pid
        if (temp.pid != stopped_pid)
        {
            tempQueue.push(temp); //push temp to tempQueue
        }
    }

    std::string logOutput = "Child " + std::to_string(stopped_pid) + " is removed from ready queue at time " +
        std::to_string(shared_clock->seconds) + " s " + std::to_string(shared_clock->nanoseconds) + " ns.";
}

int main(int argc, char* argv[])
{
    //set up alarm
    signal(SIGALRM, signal_handler);
    alarm(60);

    //initialize variables for getopt
    int opt;
    int numChildren = 1;
    int numSim = 1;
    int timeDelayNano = 100;

    while((opt = getopt(argc, argv, ":hn:s:t:f:")) != -1) //set optional args
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
                    std::cout << "-t: set time delay for children in nanoseconds\n" ;
		            std::cout << "-f: choose file for oss output\n" ;
                    std::cout << "**********************\n" ;
                    std::cout << "Example invocation: \n" ;
                    std::cout << "./oss -n 5 -s 3 -t 1000 \n" ;
                    std::cout << "Example will launch 5 child processes,";
                    std::cout << "\nwith a time delay between new children of 1000 ns\n" ;
                    std::cout << "\nand never allow more than 3 child processes to run simultaneously.\n" ;
                    return 0;
                case 'n':
                    numChildren = atoi(optarg); //assign arg value to numChildren
                    break;
                case 's':
                    numSim = atoi(optarg); //assign arg value to numSim
                    break;
                case 't':
                    timeDelayNano = atoi(optarg);
                    break;
		        case 'f':
		            logFile = optarg;
		            break;
                default:
                    std::cerr << "Please choose an option!\n" ;
                    std::cout << "Example invocation: \n" ;
                    std::cout << "./oss -n 5 -s 3 -t 1000 \n" ;
                    return 1;
            }
        }

        if (numChildren <= 0 || numSim <= 0 || timeDelayNano < 0)
        {
            std::cerr << "Please choose a valid number greater than 0." << std::endl;
            return 1;
        }
        if (numChildren > 20 || numSim > 20 || timeDelayNano > 6000000)
        {
            std::cerr << "Please choose a reasonable number. Max time: 60 s." << std::endl;
            return 1;
        }

    int shmid = shmget(SH_KEY, sizeof(Clock), IPC_CREAT | PERMS);
    if (shmid == -1)
    {
        std::cerr << "OSS: Error: Shared memory get failed" << std::endl;
        return 1;
    }

    Clock *shared_clock = static_cast<Clock*>(shmat(shmid, nullptr, 0));
    if (shared_clock == (void*)-1)
    {
        std::cerr << "OSS: Error: shmat" << std::endl;
        return 1;
    }

    shared_clock->seconds = 0;
    shared_clock->nanoseconds = 0;

    int msgid = msgget(MSG_KEY, IPC_CREAT | PERMS);
    if (msgid == -1)
    {
        std::cerr << "OSS: Error: msgget failed" << std::endl;
        return 1;
    }

    /*
    while (stillChildrenToLaunch or childrenInSystem) {
    determine if we should launch a child
    check if a blocked process should be changed to ready
    calculate priorities of ready processes
    schedule a process by sending it a message
    receive a message back and update appropriate structures
    Every half a second, output the process table to the screen and the log file
    }
    */

    int launchedChildren = 0;
    int activeChildren = 0;

    long long nextChildLaunchSec = 0;
    long long nextChildLaunchNano = 0;

    while(stillChildrenToLaunch(launchedChildren, numChildren) || stillChildrenRunning(activeChildren))
    {
        std::cout << "Looping: launchedChildren: " << launchedChildren << " activeChildren: " << activeChildren << std::endl;
        std::cout << "Time: " << shared_clock->seconds << "." << shared_clock->nanoseconds << std::endl;
        increment_clock(shared_clock, 1000);
        //determine if we should launch a new child. if no active children and no launched children, launch immediately
        if ((activeChildren == 0 && launchedChildren == 0) || timePassed(shared_clock->seconds, shared_clock->nanoseconds, nextChildLaunchSec, nextChildLaunchNano))
        {
            //launch a new child
            pid_t child_pid = fork();
            if (child_pid == -1)
            {
                std::cerr << "Error: fork failed" << std::endl;
                return 1;
            }
            else if (child_pid == 0)
            {
                //child process
                execl("./worker", "worker", nullptr);
                std::cerr << "Error: execl failed" << std::endl;
                return 1;
            }
            else
            {
                //parent process
                launchedChildren++;
                activeChildren++;
                std::cout << "Child launched: " << child_pid << std::endl;

                //find an empty spot in the PCB table
                for (int i = 0; i < MAX_PROCESSES; i++)
                {
                    if (pcb_table[i].occupied == 0)
                    {
                        //fill out the PCB table
                        pcb_table[i].occupied = 1;
                        pcb_table[i].pid = child_pid;
                        pcb_table[i].startSeconds = shared_clock->seconds;
                        pcb_table[i].startNano = shared_clock->nanoseconds;
                        pcb_table[i].serviceTimeSeconds = 0;
                        pcb_table[i].serviceTimeNano = 0;
                        pcb_table[i].eventWaitSec = 0;
                        pcb_table[i].eventWaitNano = 0;
                        pcb_table[i].blocked = 0;

                        //add to ready queue
                        readyQueue.push(pcb_table[i]);

                        std::string logMessage = "Child " + std::to_string(child_pid) + " is starting at time " +
                            std::to_string(shared_clock->seconds) + "." + std::to_string(shared_clock->nanoseconds) + ".";
                        output_to_log(logMessage);
                        //increment clock
                        increment_clock(shared_clock, 1000);
                        break;
                    }
                }

                //calculate next child launch time
                nextChildLaunchSec = shared_clock->seconds;
                nextChildLaunchNano = shared_clock->nanoseconds + timeDelayNano;
                if (nextChildLaunchNano >= BILLION)
                {
                    nextChildLaunchSec++;
                    nextChildLaunchNano -= BILLION;
                }
            }
        }

        //loop through blocked queue and check if it's time to unblock
        check_blocked_queue(shared_clock, pcb_table);

        //calculate priorities of ready processes and schedule a process by sending it a message
        schedule_process(shared_clock, msgid, pcb_table);

        //receive message back and update appropriate structures
        Message msg;
        while(msgrcv(msgid, &msg, sizeof(msg) - sizeof(long), getpid(), 0) != -1)
        {
            std::string logMessage = "Received message from child " + std::to_string(msg.pid) + "." +
                " Time slice: " + std::to_string(msg.timeSlice) + " at time " + std::to_string(shared_clock->seconds) +
                "." + std::to_string(shared_clock->nanoseconds) + ".";
            output_to_log(logMessage);

            if (msg.timeSlice < 0)
            {
                //child is done
                std::string logMessage = "Child " + std::to_string(msg.pid) + " is terminating at time " +
                    std::to_string(shared_clock->seconds) + " s and " + std::to_string(shared_clock->nanoseconds) + "ns.";
                output_to_log(logMessage);

                activeChildren--;

                //remove from PCB table
                remove_from_PCB(msg.pid);

                //remove from ready queue
                remove_from_ready(msg.pid, shared_clock);

                long long timeUsedByChild = (0 - msg.timeSlice); //time used by child
                increment_clock(shared_clock, timeUsedByChild);
                break;
            }
            else if (msg.timeSlice > 0)
            {
                //check if msg timeSlice == 5000 ns, if so, worker is not blocked; otherwise worker is blocked for timeSlice in ns
                if (msg.timeSlice == 5000)
                {
                    //worker is not blocked
                    for (int i = 0; i < MAX_PROCESSES; i++)
                    {
                        if (pcb_table[i].pid == msg.pid)
                        {
                            pcb_table[i].serviceTimeSeconds += shared_clock->seconds - pcb_table[i].startSeconds;
                            pcb_table[i].serviceTimeNano += shared_clock->nanoseconds - pcb_table[i].startNano;
                            pcb_table[i].startSeconds = shared_clock->seconds;
                            pcb_table[i].startNano = shared_clock->nanoseconds;
                            break;
                        }
                    }
                }
                else
                {
                    //worker is blocked
                    for (int i = 0; i < MAX_PROCESSES; i++)
                    {
                        if (pcb_table[i].pid == msg.pid)
                        {
                            pcb_table[i].eventWaitSec = shared_clock->seconds;
                            pcb_table[i].eventWaitNano = shared_clock->nanoseconds + msg.timeSlice;
                            if (pcb_table[i].eventWaitNano >= BILLION)
                            {
                                pcb_table[i].eventWaitSec++;
                                pcb_table[i].eventWaitNano -= BILLION;
                            }
                            pcb_table[i].blocked = 1;
                            //add to blocked queue
                            blockedQueue.push(pcb_table[i]);
                            //remove from ready queue
                            remove_from_ready(msg.pid, shared_clock);
                            break;
                        }
                    }
                }
            }
            break;
        }
    }

    //clean up
    shmdt(shared_clock);
    shmctl(shmid, IPC_RMID, nullptr);
    shmctl(msgid, IPC_RMID, nullptr);

    return 0;
}