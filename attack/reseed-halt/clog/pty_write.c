#include <stdio.h> 
#include <stdlib.h>     
#include <unistd.h> 
#include <string.h>
#include <pty.h> 
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>

void *message;
size_t message_size;

typedef struct 
{
	int master_fd;
	int slave_fd;
} pty_pair_t;

void* writer_thread(void* arg)
{
	pty_pair_t* pty = (pty_pair_t*)arg;

	while (1) {
		write(pty->master_fd, message, message_size);
		read(pty->slave_fd, message, message_size);
	}
	return NULL;
}

int main(int argc, char *argv[]) 
{
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <NUM_THREADS> <NUM_PAGES>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int num_threads = atoi(argv[1]);
	int num_pages = atoi(argv[2]);

	char slave_names[num_threads][100];
	pthread_t writer_tids[num_threads];
	pthread_t reader_tids[num_threads];
	pty_pair_t pty_pairs[num_threads];

	message_size = getpagesize() * num_pages;
	message = mmap(NULL, message_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	int urandom_fd = open("/dev/urandom", O_RDONLY);
	ssize_t bytes_read = read(urandom_fd, message, message_size);
	close(urandom_fd);

	for (int i = 0; i < num_threads; ++i) {
		openpty(&pty_pairs[i].master_fd, &pty_pairs[i].slave_fd, slave_names[i], NULL, NULL);
		pthread_create(&writer_tids[i], NULL, writer_thread, &pty_pairs[i]);
	}

	for (int i = 0; i < num_threads; ++i) {
		pthread_join(writer_tids[i], NULL);
	}

	munmap(message, message_size);

	return 0;
}

