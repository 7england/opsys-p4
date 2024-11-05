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
    int blocked; //is process blocked?
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
    std::cout << message << std::endl; //write message to screen
    totalLines++; //increment line count

    logFileStream.close(); //close file
}

void print_process_table(PCB pcb_table[], Clock* shared_clock)
{
    //print process table to log and screen
    std::string logOutput = "OSS PID: " + std::to_string(getpid()) + " Time: " + std::to_string(shared_clock->seconds) + "." + std::to_string(shared_clock->nanoseconds) + "\n";
    logOutput += "PID\tStart Time Seconds\tStart Time Nano\tService Time Seconds\tService Time Nano\tEvent Wait Seconds\tEvent Wait Nano\tBlocked\n";
    logOutput+= "------------------------------------------------------------------------------------------------------------------------------------\n";
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (pcb_table[i].occupied == 1)
        {
            logOutput += std::to_string(pcb_table[i].pid) + "\t\t" + std::to_string(pcb_table[i].startSeconds) + "\t\t" + std::to_string(pcb_table[i].startNano) + "\t\t" +
                std::to_string(pcb_table[i].serviceTimeSeconds) + "\t\t" + std::to_string(pcb_table[i].serviceTimeNano) + "\t\t" +
                std::to_string(pcb_table[i].eventWaitSec) + "\t\t" + std::to_string(pcb_table[i].eventWaitNano) + "\t\t" +
                std::to_string(pcb_table[i].blocked) + "\n";
        }
    }
    logOutput += "------------------------------------------------------------------------------------------------------------------------------------\n";

    output_to_log(logOutput);
}

void remove_from_PCB(pid_t dead_pid)
{
    //std::cout << "PID to delete: " << dead_pid << std::endl;
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
    if (sec1 > sec2)
    {
        return true;
    }
    else if (sec1 == sec2 && nano1 >= nano2)
    {
        return true;
    }
    else
    {
        return false;
    }
}

long double calculate_priority(PCB pcb, Clock* shared_clock)
{
    //calculate total system time in seconds and nanoseconds
    long double totalSystemSeconds = shared_clock->seconds;
    long double totalSystemNano = shared_clock->nanoseconds;

    if (totalSystemSeconds == 0 && totalSystemNano == 0)
    {
        return 0;
    }

    //calculate total service time in seconds and nanoseconds
    long double totalServiceSeconds = pcb.serviceTimeSeconds;
    long double totalServiceNano = pcb.serviceTimeNano;

    if (totalServiceSeconds == 0 && totalServiceNano == 0)
    {
        return 0;
    }

    //convert nanoseconds to seconds
    long double totalSystemTime = totalSystemSeconds + (totalSystemNano / BILLION);
    long double totalServiceTime = totalServiceSeconds + (totalServiceNano / BILLION);

    //calculate priority as the ratio of total service time to total system time
    long double priority = totalServiceTime / totalSystemTime;
    return priority;
}


void print_ready_queue(Clock* shared_clock)
{
    std::string logOutput = "Ready Queue:\n";
    logOutput += "PID\tPriority\n";
    logOutput += "-----------------\n";

    std::queue<PCB> tempQueue = readyQueue;
    while (!tempQueue.empty())
    {
        PCB temp = tempQueue.front();
        tempQueue.pop();
        long double priority = calculate_priority(temp, shared_clock);
        logOutput += std::to_string(temp.pid) + "\t" + std::to_string(priority) + "\n";
    }

    logOutput += "-----------------\n";
    output_to_log(logOutput);

    //std::cout << logOutput << std::endl;
}

bool schedule_process(Clock *shared_clock, int msgid, PCB pcb_table[])
{
    //std::cout << "Scheduling process..." << std::endl;
    //find the lowest priority process; closer to 0 should be scheduled first
    long double lowestPriority = 1.0;
    PCB lowestPriorityProcess;

    if (!readyQueue.empty())
    {
        std::queue<PCB> tempQueue;
        while (!readyQueue.empty())
        {
            PCB temp = readyQueue.front(); //get front of queue
            readyQueue.pop(); //pop front of queue
            long double priority = calculate_priority(temp, shared_clock);
            //if priority is closer to 0, set as lowest priority
            if (priority < lowestPriority)
            {
                lowestPriority = priority;
                lowestPriorityProcess = temp;
            }
            tempQueue.push(temp); //push temp to tempQueue
        }

        //repopulate ready queue
        while (!tempQueue.empty())
        {
            PCB temp = tempQueue.front(); //get front of queue
            tempQueue.pop(); //pop front of queue
            readyQueue.push(temp); //push temp to readyQueue
        }

        //send message to lowest priority process
        Message msg;
        msg.msgtype = lowestPriorityProcess.pid;
        msg.pid = getpid();
        msg.timeSlice = 5000; //time slice of 5000 ns
        if (msgsnd(msgid, &msg, sizeof(msg) - sizeof(long), 0) == -1)
        {
            std::cerr << "Error: msgsnd failed" << std::endl;
            return false;
        }
        else
        {
            std::string logOutput = "Sent message to child " + std::to_string(lowestPriorityProcess.pid) + " with priority " +
                std::to_string(lowestPriority) + " at time " + std::to_string(shared_clock->seconds) + "." + std::to_string(shared_clock->nanoseconds) + ".";
            output_to_log(logOutput);
            return true;
        }
    }
    else
    {
        return false;
    }

    return true;
}

void check_blocked_queue(Clock *shared_clock, PCB pcb_table[])
{
    std::queue<PCB> tempQueue;
    while (!blockedQueue.empty())
    {
        PCB temp = blockedQueue.front(); //get front of queue
        blockedQueue.pop(); //pop front of queue
        //check if event has occurred
        if (shared_clock->seconds >= temp.eventWaitSec && shared_clock->nanoseconds >= temp.eventWaitNano)
        {
            //add to ready queue
            readyQueue.push(temp);
            std::string logOutput = "Child " + std::to_string(temp.pid) + " is unblocked at time " +
                std::to_string(shared_clock->seconds) + " s " + std::to_string(shared_clock->nanoseconds) + " ns.";
            output_to_log(logOutput);
            increment_clock(shared_clock, 100000); //increment clock by 100000 ns to simulate unblocking taking longer
        }
        else
        {
            tempQueue.push(temp); //push temp to tempQueue
        }
    }
    //repopulate blocked queue
    while (!tempQueue.empty())
    {
        PCB temp = tempQueue.front(); //get front of queue
        tempQueue.pop(); //pop front of queue
        blockedQueue.push(temp); //push temp to blockedQueue
    }
}

void remove_from_ready(pid_t stopped_pid, Clock* shared_clock)
{
    std::queue<PCB> tempQueue;
    bool found = false;
    while (!readyQueue.empty())
    {
        PCB temp = readyQueue.front(); // get front of queue
        readyQueue.pop(); // pop front of queue
        // check if pid is not equal to message pid
        if (temp.pid != stopped_pid)
        {
            tempQueue.push(temp); // push temp to tempQueue
        }
        else
        {
            found = true;
        }
    }

    // repopulate ready queue
    while (!tempQueue.empty())
    {
        PCB temp = tempQueue.front(); // get front of queue
        tempQueue.pop(); // pop front of queue
        readyQueue.push(temp); // push temp to readyQueue
    }

    if (found)
    {
        std::string logOutput = "Child " + std::to_string(stopped_pid) + " is removed from ready queue at time " +
            std::to_string(shared_clock->seconds) + " s " + std::to_string(shared_clock->nanoseconds) + " ns.";
        output_to_log(logOutput);
    }
}

int main(int argc, char* argv[])
{
    //set up alarm
    signal(SIGALRM, signal_handler);
    signal(SIGINT, signal_handler);
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
    int blockedCounter = 0;
    int lastPrintedSec = -1;

    long long nextChildLaunchSec = 0;
    long long nextChildLaunchNano = 0;
    long long lastPrintedNano = -1;

    while(stillChildrenToLaunch(launchedChildren, numChildren) || stillChildrenRunning(activeChildren))
    {
        //std::cout << "Looping: launchedChildren: " << launchedChildren << " activeChildren: " << activeChildren << std::endl;
        //std::cout << "Time: " << shared_clock->seconds << "." << shared_clock->nanoseconds << std::endl;
        increment_clock(shared_clock, 10000); //increment clock by 10000 ns every loop to keep things moving
        //determine if we should launch a new child. if no active children and no launched children, launch immediately
        if ((activeChildren == 0 && launchedChildren == 0) || ((activeChildren < numSim && launchedChildren < numChildren) && (timePassed(shared_clock->seconds, shared_clock->nanoseconds, nextChildLaunchSec, nextChildLaunchNano))))
        {
            //std::cout << "time passed: " << shared_clock->seconds << "." << shared_clock->nanoseconds << std::endl;
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
                //std::cout << "Child launched: " << child_pid << std::endl;

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

                        std::string logMessage = "Child " + std::to_string(child_pid) + " is launching at time " +
                            std::to_string(shared_clock->seconds) + "." + std::to_string(shared_clock->nanoseconds) + ".";
                        output_to_log(logMessage);
                        //increment clock
                        increment_clock(shared_clock, 10000);
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
        check_blocked_queue(shared_clock, pcb_table); //this also increments clock 100000ns when it unblocks a process

        //print ready queue if not empty
        if(!readyQueue.empty())
            print_ready_queue(shared_clock);

        //calculate priorities of ready processes and schedule a process by sending it a message; set flag
        bool flag = schedule_process(shared_clock, msgid, pcb_table);

        //check to make sure a process was scheduled before receiving message; otherwise, loop again
        if (!flag)
        {
            continue;
        }

        //receive message back and update appropriate structures if a process was scheduled
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
                std::string logMessage = "Child " + std::to_string(msg.pid) + " ran at time " + std::to_string(shared_clock->seconds) +
                    "." + std::to_string(shared_clock->nanoseconds) + " and sent time slice " + std::to_string(msg.timeSlice) + " ns.";
                output_to_log(logMessage);

                //check if msg timeSlice == 5000 ns, if so, worker is not blocked; otherwise worker is blocked for timeSlice in ns
                if (msg.blocked == 0)
                {
                    //use timeslice to calculate time spent as percentage of 5000 where (1/p) * 5000 = time spent
                    std::string logMessage = "Child " + std::to_string(msg.pid) + " is not blocked and ran for " + std::to_string(msg.timeSlice) + " ns.";
                    output_to_log(logMessage);
                    //worker is not blocked
                    for (int i = 0; i < MAX_PROCESSES; i++)
                    {
                        if (pcb_table[i].pid == msg.pid)
                        {
                            //add service time
                            pcb_table[i].serviceTimeNano += msg.timeSlice;
                            std::cout << "Service time: " << pcb_table[i].serviceTimeNano << std::endl;
                            if (pcb_table[i].serviceTimeNano >= BILLION)
                            {
                                pcb_table[i].serviceTimeSeconds++;
                                pcb_table[i].serviceTimeNano -= BILLION;
                            }

                            //recalculate priority
                            long double priority = calculate_priority(pcb_table[i], shared_clock);
                            //delete old instance from ready queue
                            std::string clarify = "Child " + std::to_string(msg.pid) + " duplicate in ready queue to be removed.";
                            output_to_log(clarify);
                            remove_from_ready(msg.pid, shared_clock);
                            //add to ready queue
                            readyQueue.push(pcb_table[i]);

                            //increment clock
                            increment_clock(shared_clock, 5000);
                            break;
                        }
                    }
                }
                else if (msg.blocked == 1 && msg.timeSlice > 0)
                {
                    blockedCounter++;
                    std::string blockedCounterMsg = "Blocked counter: " + std::to_string(blockedCounter);
                    output_to_log(blockedCounterMsg);

                    //check if timeSlice is more than one second. if it is, split it into seconds and nano. then adjust block time for that pid
                    if(msg.timeSlice >= BILLION)
                    {
                        std::string logMessage = "Child " + std::to_string(msg.pid) + " is blocked for " + std::to_string(msg.timeSlice) + " ns.";
                        output_to_log(logMessage);
                        //split timeSlice into seconds and nanoseconds
                        long long seconds = msg.timeSlice / BILLION;
                        long long nano = msg.timeSlice % BILLION;

                        //adjust pcb table and add to blocked queue
                        for (int i = 0; i < MAX_PROCESSES; i++)
                        {
                            if (pcb_table[i].pid == msg.pid)
                            {
                                //update service time with time used
                                pcb_table[i].serviceTimeNano += 5000;
                                if (pcb_table[i].serviceTimeNano >= BILLION)
                                {
                                    pcb_table[i].serviceTimeSeconds++;
                                    pcb_table[i].serviceTimeNano -= BILLION;
                                }
                                pcb_table[i].eventWaitSec = shared_clock->seconds + seconds;
                                pcb_table[i].eventWaitNano = shared_clock->nanoseconds + nano;
                                pcb_table[i].blocked = 1;

                                //add to blocked queue
                                blockedQueue.push(pcb_table[i]);
                                //remove from ready queue
                                remove_from_ready(msg.pid, shared_clock);
                                break;
                            }
                        }
                    }
                    else
                    {
                        //adjust pcb table and add to blocked queue
                        for (int i = 0; i < MAX_PROCESSES; i++)
                        {
                            if (pcb_table[i].pid == msg.pid)
                            {
                                //update wait time
                                pcb_table[i].eventWaitSec = shared_clock->seconds;
                                pcb_table[i].eventWaitNano = shared_clock->nanoseconds + msg.timeSlice;
                                pcb_table[i].blocked = 1;
                                //update service time with time used
                                pcb_table[i].serviceTimeNano += 5000;
                                if (pcb_table[i].serviceTimeNano >= BILLION)
                                {
                                    pcb_table[i].serviceTimeSeconds++;
                                    pcb_table[i].serviceTimeNano -= BILLION;
                                }

                                //add to blocked queue
                                blockedQueue.push(pcb_table[i]);
                                //remove from ready queue
                                remove_from_ready(msg.pid, shared_clock);
                                break;
                            }
                        }
                    }

                }
                else
                {
                    std::cerr << "Error: Invalid time slice." << std::endl;
                }
            }
            break;
        }

        std::string timeCheckForPrint = "Time check for print: " + std::to_string(shared_clock->seconds) + " " + std::to_string(shared_clock->nanoseconds) + " " + std::to_string(lastPrintedSec) + " " + std::to_string(lastPrintedNano);
        //check to see if .5 s passed
        if (shared_clock->seconds > lastPrintedSec || (shared_clock->seconds >= lastPrintedSec && shared_clock->nanoseconds >= lastPrintedNano + 500000000))
        {
            //print active children
            std::string msg = "\nActive children: " + std::to_string(activeChildren);
            //print launched children
            msg += "\nLaunched children: " + std::to_string(launchedChildren);
            //print blocked queue size
            msg += "\nBlocked queue size: " + std::to_string(blockedQueue.size());
            //print throughput
            msg += "\nThroughput: " + std::to_string(launchedChildren) + " processes running in " +
                std::to_string(shared_clock->seconds) + " seconds and " + std::to_string(shared_clock->nanoseconds) + " nanoseconds.";
            //print time since last print
            msg += "\nTime since last print: " + std::to_string(shared_clock->seconds - lastPrintedSec) + " seconds and " +
                std::to_string(shared_clock->nanoseconds - lastPrintedNano) + " nanoseconds.";

            //print msg
            output_to_log(msg);
            //print process table
            print_process_table(pcb_table, shared_clock);
            //print_output(shared_clock, activeChildren, launchedChildren);
            lastPrintedSec = shared_clock->seconds;
            lastPrintedNano = shared_clock->nanoseconds;
        }

    }

    //clean up
    shmdt(shared_clock);
    shmctl(shmid, IPC_RMID, nullptr);
    shmctl(msgid, IPC_RMID, nullptr);

    return 0;
}