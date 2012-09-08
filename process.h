/* Process class (POSIX)
 * - spawn/kill a child process
 * - "talk" to the child process via pipes
 * TODO:
 * - timeout read function, using select() system call
 * */
#pragma once
#include <stdbool.h>
#include <sys/types.h>
#include <stdio.h>

struct ProcessErr {};
struct IOErr: ProcessErr {};

class Process
{
public:
	Process(): pid(0), in(NULL), out(NULL) {}
	virtual void create(const char *cmd) throw (ProcessErr);
	virtual ~Process();

protected:
	pid_t pid;
	FILE *in, *out;

	void cleanup();
	void write_line(const char *s);
	void read_line(char *s, int n);
};
