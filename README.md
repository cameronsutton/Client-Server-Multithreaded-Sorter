# What This Is
This is a project created for an Operating System's class I took. The premise is there is a client/server structure where the client sends list of numbers to be sorted to the server. The server then sorts the numbers using a multithreaded implementation of mergesort, and sends the result back to the client.

# How It Works

This project can be split into 3 sections: Cal, Admin, and Client.

## Cal
Cal is the implementation of the multithreaded sorting algorithm, and is in my opinion the most interesting part of this project. The only thing provided for the cal file was a description on how it interacts with admin, how the threads should be structured relative to each other, and what the expected output should be in debug mode.

### Design
Cal can be split into 4 distinct sections
- Main Thread
- Input Threads
- Sorter Threads
- Merger Threads

The following is a diagram of how an input thread, sorting threads, and merging threads relate to each other.
![image](https://github.com/cameronsutton/Client-Server-Multithreaded-Sorter/assets/165424172/d1258b6c-2331-4c35-84d5-bbe44fce55cf)

It should be noted that I used uint8_t, uint64_t, and uint16_t in many places rather than just plain int. This
is for two reasons. One is that due the extensive memory overhead I tried to save some memory. The
other is for inter-system compatibility. I developed this on Windows which has 64-bit pointers. CentOS
7.9 (what the university linux servers use and what this project was tested on) has 32-bit pointers. Using these defined size types help prevent
any potential bugs between the different environments.

### Main Thread

The main thread is responsible for initialization of all the other threads and reading input from the server

#### Memory Initialization
Every initialized piece of data is stored in its own array. This makes it relatively easy to distribute
the necessary information to each thread at thread creation time. There are 20 arrays dedicated to
information and they are as follows:

1. An array containing all the input arrays
2. An array containing flags for which input arrays are currently busy
3. Thread IDs of the input threads
4. Mutexes for use in signaling an input thread to begin
5. Condition variables for use in signaling an input thread to begin
6. Conditions that control which input thread should begin
7. Thread IDs of the sorter threads
8. Mutexes for use in signaling a specific sorter thread to begin
9. Condition variables for use in signaling a specific sorter thread to begin
10. Conditions that control which sorter thread should begin sorting
11. Thread IDS of the merger threads
12. Mutexes for use in signaling a specific merger thread to begin
13. Condition variables for use in signaling a specific merger thread to begin
14. Conditions that control which merger thread should begin merging
15. Mutexes for a counter used in synchronizing each merger layer for debug printing
16. Condition variables for a counter used in synchronizing each merger layer for debugging
17. Counters that each merger thread increment to indicate it is done merging
18. Mutexes for signaling all merger threads in a layer to begin executing again after debug
printing
19. Condition variables for signaling all merger threads in a layer to begin executing again after
debug printing
20. Conditions that control when the merger threads in a layer should continue execution
    
Each input array gets one of all these arrays dedicated to it and are separate from each other

The input arrays themselves do not need and mutexes as all the sorter/merger threads only modify
their specified part at any given time, so by synchronizing the sorters/mergers you prevent any
conflict on the main input array. It also allows the input array to have different parts worked on
simultaneously.

The input arrays are initialized to have the max integer value in every slot. This is so that after
sorting everything, the data is contained at the very start of the array and not at the end of the
array.

#### Thread Creation

Due to the intricacy of the synchronization mechanism, my main goal with the threads was to
have them do as little array index calculation as possible. This ultimately meant giving each
thread many different arguments for every single pointer it would need.

The input threads each have 18 arguments
The merger threads each have 19 arguments
The sorter threads each have 9 arguments

To pass the arguments, I created a 19-element array of type (uint64_t). I chose unsigned 64 bit
integers since I am passing everything by reference, so the 64-bit type size lets the pointers fit
without overflowing.

Then I cast all the pointers to (uint64_t) when adding them to the array. This is for compatibility
with the 32-bit pointers of the university’s linux server.

Finally, I call pthread_create and give this array as a parameter. I also then sleep for a short time
to allow the thread to initialize itself before the array is overwritten from the next thread’s
initialization.

#### Admin Input
The main loop consists first of a loop that waits for some input to come through the admin/cal
pipe. There is also a mutex/condition variable around this loop to prevent the main thread from
reading input from pipe at the same time an input thread is reading input from the pipe.

The first number that comes through the pipe will be the length of the data that is to be sorted.
After receiving this, the main thread will use Array #2 as specified above to find an unused input
thread and then signal that input thread to begin.

#### Pseudocode for main thread
```
Initialize memory
Create threads
While true:
    Wait for input from pipe (input will be the length of the incoming array)
    If input is -1 then shutdown cal
    Find a free input thread
    Signal the free input thread to begin
    Lock self until input thread is done reading
```
### Input Thread
The input thread is responsible for signaling the sorter threads to begin as well as signaling the merger
threads to continue if debug printing is enabled. It is also responsible for all printing of the array.

The input thread spends most of its time locked by a condition variable. The only way it gets unlocked is
if it is signaled from the main thread, and if it is signaled from the main thread that means there must be
data in the pipe to read.

The data is then read in the order CID, File name length, File name, Numbers. The numbers are read
directly into the input array. If debug printing is enabled, the input array numbers are also printed as
Layer 1

Next all the sorter threads are signaled to start by updating their conditions and then signaling them.

If debug printing is enabled, then there are several steps to be taken next. These next steps are repeated
for each merger layer in a for loop.
1. Wait for all the threads on a certain layer to finish. This is done by monitoring a counter
associated with that layer. Once the counter reaches a specific value, the merger thread knows
all the threads on that layer are done and will signal the input thread to continue
2. Print out the input array.
3. Broadcast to all the merger threads in the layer to continue executing.

Then the input thread will wait until the last merger layer finishes. It does this by waiting on a condition
that the last merger layer signals when it finishes.

Then after printing the sorted array, it sets all the variables to indicate it is unused and then unlocks the
mutex controlling its execution.

#### Pseudocode for input thread:
```
Wait for signal from main thread
Read data from pipe buffer into array
Signal all sorter threads to begin
if D (debug) is 1
    for each merger layer
        Wait for all merger threads in layer to finish merging
        Print out the current state array
        Signal merger threads to continue merging
Wait for final merger thread to finish
Print sorted array and reset array to initial state.
```

### Sorter Thread
The sorter threads are responsible for sorting each initial segment of the array as defined by M.

It does with by waiting on a condition to begin sorting (signaled from the input thread), calling qsort(),
signaling the merger thread associated with it, and then resetting its condition.

#### Pseudocode for sorter thread
```
Wait for signal from input thread
Sort designated array segment
Signal associated merger thread it is done
```

### Merger Thread
The merger thread is responsible for merging two segments of the array together.

To do this, it must first way until the two segments it wants to merge are sorted. It does this by waiting on
two condition variables, one for the left segment and one for the right segment.

If debug printing is enabled, some more synchronization is needed since the merger threads only
condition for executing is if the two threads above it finish. It normally does not care about the other
threads on its layer.

This is achieved with a counter associated with the layer. Incrementing this counter is controlled with a
mutex, so if the counter becomes equal to the number of merger threads in that layer, then all the merger
threads in the layer must be done. If this is the case, then the input thread is signaled. It then waits on a
condition variable that the input thread will signal once it is finished printing the array.

It then merges the two halves together, I used a simple algorithm similar to bubble sort for this since it is
simple, easy to debug, and requires no array copying to achieve.

The conditions are then reset to prepare itself for the next sorting task, and then the associated merger
thread in the next layer is then to go.

#### Pseudocode for merger thread
```
Wait for signal that left side is done merging/sorting
Wait for signal that right side is done merging/sorting
If D (debug) is 1
    Signal that previous layer merging is finished
    Wait for signal that input thread is done printing
Merge left side and right side
Reset left and right signals
Signal next layer’s merger thread that merging is done
```


## Server (admin)
The admin program is responsible for communication with clients and forwarding data to cal.

The first thing admin does is create a pipe, fork(), and then call execvp() in the child process to create the
cal process.

Then the parent process communicates with client and forward data to the created cal process.

It first initializes itself by setting up a socket listening on port 17710 and setting up a signal handler for
SIGINT for exiting the program.

It then waits on select() for any update to the tracked file descriptors. At first, the only file descriptor is
the main socket listener, but as clients connect their descriptors are added to the file descriptor set for
tracking as well.

When one of the file descriptors gets data, admin will loop through all the file descriptors in the set to
look for which one has been updated. If it is the listener socket, that means a new client has connected so
its descriptor should be added to the set. Otherwise, it must be data from an existing connection, so the
data is read from the socket into associated buffers.

The data consists of a stream of integers of the form `<CID> <FILE NAME LENGTH> <FILE NAME>
<LENGTH OF DATA TO SORT> <DATA TO SORT>`.

The data is then written to the pipe for the cal process to read.

However, if the read data returns zero, that means the file descriptor update was from the client closing
the connection. In this case, the descriptor is removed from the set rather than cal being called.

If CTRL+C is pressed, the signal handler sets the variable “exit_signal” to one. The entire client-admin-
cal communication is contained in a while loop that only executes while exit_signal is zero. When
exit_signal becomes one, the exit signal of -1 is written to cal and then after cal has terminated, admin
terminates.

#### Pseudocode:
```
Create pipe
fork
if child:
    execvp(cal)
if parent:
    create listener socket
    setup signal handler
    while not exit signal:
        wait for update to file descriptors
        find which descriptor was updated
        if update from listener:
            add client to file descriptor set
        else:
            Receive data
            if receive returned 0:
                remove client from file descriptor set
            else:
                write received data to cal
```

## Client
The client is responsible for reading in data from a file and then sending it to admin via socket. The data it
sends through the socket is a single stream of integers all contained in a single buffer array. The first
element is the CID, the second element is the File Length, then comes the file name, followed by the
length of the numbers array and finally the actual numbers.

It first creates a socket and connects to the admin process with the specified IP/port. It also sets the first
element of the buffer to CID, as this will never change no matter what request the makes.

After receiving a file name from the user, it will read the file name length and file name into the buffer.
The file name can not be read directly into the buffer though since the buffer is of type int, which is larger
than char. So instead, the file name is read into a smaller buffer and read char-by-char into the main
buffer. Then the length of the numbers array and the numbers themselves are written to the buffer.

The buffer is then sent to admin, and the client loops and requests a file name again.

# Building and Running
This is only tested to build and run on CentOS 7.9

1. Run “make” to compile the files
Launch admin with “./admin.exe <M> <A> <Q> <D>

- M is the number of merger threads to be used in each sorting instance, must be a power of 2 and at most 512
- A is the number of sorting instances
- Q is unused
- D is the debug flag

Due to a bug, Q is used as the debug flag and D is unused

The arguments `512 2 10 1` will launch admin with settings to launch cal with 512 merger threads per sorting instance, 2 sorting instances, and with debug enabled since Q is not 0.

Launch client with “./client.exe <CID> <IP> <Port>”

- The CID (Client ID) can be anything
- The IP can be 0.0.0.0 if it is being run locally
- The port is 17710

Some sample arguments would be `100 0.0.0.0 17710`

The client will ask for a file to be sorted, this file should be formatted `<Number of Elements To Sort> <Numbers to Sort>` and must be in the same working directory as the client executable. A sample file `sortreq` has been provided.
