# Introduction

In this assignment, we will complete the implementation of a message queue by incorporating the necessary locks and condition variable operations to synchronize access to the message queue. Additionally, we will implement I/O multiplexing functionality, inspired by the `poll()` system call.

## Overview

The task involves enhancing a message queue implementation by synchronizing access through locks and condition variables. We will model the I/O multiplexing functionality after the `poll()` system call.

### Part 1: Message Queue Implementation

A message queue, distinct from a pipe, stores individual messages preceded by their length. Your task is to implement functions in `msg_queue.c`, excluding `msg_queue_poll()`. These functions interact with the message queue, and the descriptions are provided in `msg_queue.h`.

### Part 2: Implementing `msg_queue_poll()`

`msg_queue_poll()` monitors multiple message queues for I/O events. Its API mirrors that of the `poll()` system call. You will implement this function in three stages, as detailed below.

## Part 1: Message Queue Implementation

### Task

- Implement functions in `msg_queue.c`, except `msg_queue_poll()`.
- Focus on `msg_queue_read()` and `msg_queue_write()` functions, ensuring correct synchronization.
- Initially, implement blocking versions of these functions. Later, add support for non-blocking semantics.

## Part 2: Implementing `msg_queue_poll()`

### Task

- Implement `msg_queue_poll()` to wait until one of multiple queues is ready for I/O.
- The API resembles that of the `poll()` system call.
- Your implementation should consist of three stages, as outlined below.

### Implementation Stages

1. **Check for Triggered Events:**
   - Verify if any requested events on monitored queues have already occurred.

2. **Subscribe to Events:**
   - If no events have occurred, block until another thread triggers an event.
   - Use condition variables for blocking and signaling.

3. **Handle Triggered Events:**
   - After blocking or if no blocking is needed, determine which events have been triggered.
   - Return the number of queues with triggered events.

### Important Details

- Utilize the provided synchronization functions and follow the instructions in the starter code.
- Make use of the linked list implementation in `list.h` for the wait queue.

For further understanding, refer to the provided code and additional resources as needed.
