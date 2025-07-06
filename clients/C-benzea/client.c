/* gcc -o client client.c $( pkg-config --libs --cflags openssl ) -g */

#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <string.h>
#include <openssl/sha.h>
#include <sys/time.h>

void shuffle(uint32_t *array, size_t n) {    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int usec = tv.tv_usec;
    srand48(usec);

    if (n > 1) {
        size_t i;
        for (i = n - 1; i > 0; i--) {
            size_t j = (unsigned int) (drand48()*(i+1));
            int t = array[j];
            array[j] = array[i];
            array[i] = t;
        }
    }
}

struct queue {
	int wr;
	int rd;
	int size;
	uint32_t queue[];
};

#define SOLVE_QUEUE_SIZE 4
struct queue *solve_queue;

#define offset_of(t, m) ((size_t) &((t *)0)->m)
#define offset_of_end(t, m) (offset_of(t, m) + sizeof(((t *)0)->m))

struct __attribute__((__packed__)) coordinate {
	uint16_t x;
	uint16_t y;
};

struct __attribute__((__packed__)) px_state {
	uint16_t x;
	uint16_t y;
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t difficulty;
	uint8_t challenge[16];
	uint8_t nonce[16];
	uint8_t new_r;
	uint8_t new_g;
	uint8_t new_b;
	// timestamp?
};


int width;
int height;
int x_offset;
int y_offset;
uint8_t *image;
int request_sock;
int set_sock;
struct sockaddr_in server_addr;

struct px_state *image_state;

uint64_t px_requested;
uint64_t px_received;
int stat_px_received;
int stat_px_solved;


void *pixel_requester(void *arg) {
	uint32_t order[width * height];

	for (int i; i < width * height; i++) {
		order[i] = i;
	}
	shuffle(order, width * height);

	struct timespec start;
	clock_gettime(CLOCK_MONOTONIC, &start);


	while (1) {
		struct timespec ts = {
			.tv_sec = 0,
			.tv_nsec = 10000000,
		};
		struct timespec now;
		double diff;

		for (int i = 0; i < width * height; i++) {
			int x = order[i] % width;
			int y = order[i] / width;
			int prev_wr = (solve_queue->wr - 1) % solve_queue->size;
			struct coordinate coordinate = {
				.x = x + x_offset,
				.y = y + y_offset,
			};

			/*
			 * Cap requests at 1.5 the amount of received packets.
			 */
			//printf("%ld, %ld\n", px_requested, px_received);
			if (px_requested > px_received * 2 + 1000) {
				syscall(__NR_futex, &px_received,
					FUTEX_WAIT, (uint32_t) px_received, &ts);
			}
			px_requested += 1;
			write(request_sock, &coordinate, sizeof(coordinate));
		}

		clock_gettime(CLOCK_MONOTONIC, &now);

		diff = now.tv_sec - start.tv_sec;
		diff += (now.tv_nsec - start.tv_nsec) / (double) 1000000000;
		printf("Full lossy frame: %f seconds\n", diff);

		start = now;
	}
	return NULL;
}

void *pixel_receiver() {
	struct px_state px_state;

	while (1) {
		int offset;
		int new_wr = (solve_queue->wr + 1) % solve_queue->size;
		int ret;

		//printf("RECEIVER HUHU!\n");

		/* Stop if the queue is filled */
		while (solve_queue->rd == new_wr) {
			syscall(__NR_futex, &solve_queue->rd,
				FUTEX_WAIT, new_wr, NULL);
		}

		//printf("reading response %d, %d\n", solve_queue->rd, new_wr);
		ret = read(request_sock, &px_state, sizeof(px_state));
		if (px_state.x - x_offset >= width || px_state.y - y_offset >= height)
			continue;
		//printf("READ %d; %d, %d\n", ret, px_state.x, px_state.y);
		offset = px_state.x - x_offset + (px_state.y - y_offset) * width;
		memcpy(&image_state[offset], &px_state, sizeof(px_state));

		solve_queue->wr = new_wr;
		solve_queue->queue[new_wr] = offset;
		syscall(__NR_futex, &solve_queue->wr, FUTEX_WAKE, 1);

		px_received += 1;
		syscall(__NR_futex, &stat_px_received, FUTEX_WAKE, 1);
		__atomic_add_fetch(&stat_px_received, 1, __ATOMIC_ACQUIRE);
	}

	return NULL;
}

void *pixel_receiver_set() {
	struct px_state px_state;
	struct timespec start;
	struct timespec now;
	double diff;
	int pixel = 0;

	clock_gettime(CLOCK_MONOTONIC, &start);

	while (1) {
		int offset;
		int new_wr = (solve_queue->wr + 1) % solve_queue->size;
		int ret;

		ret = read(set_sock, &px_state, sizeof(px_state));
		if (++pixel == 5000) {
			int px_received_last = 0;
			int px_solved_last = 0;
			clock_gettime(CLOCK_MONOTONIC, &now);

			diff = now.tv_sec - start.tv_sec;
			diff += (now.tv_nsec - start.tv_nsec) / (double) 1000000000;

			px_received_last = __atomic_exchange_n(&stat_px_received, 0, __ATOMIC_ACQUIRE);
			px_solved_last = __atomic_exchange_n(&stat_px_solved, 0, __ATOMIC_ACQUIRE);

			printf("%0.5f px/s, challenges: %0.5f px/s (%f seconds for %d pixel and %d challenges, solved: %d)\n", (double)pixel / diff, px_received_last / diff, diff, pixel, px_received_last, px_solved_last);



			start = now;
			pixel = 0;
		}
	}

	return NULL;
}

void solve_pixel(struct px_state *px_state)
{
	unsigned char hash[SHA256_DIGEST_LENGTH] __attribute__((aligned(8)));
	SHA256_CTX sha256;

	do {
		SHA256_Init(&sha256);
		*(uint32_t*)&px_state->nonce += 1;

		SHA256_Update(&sha256, px_state, offset_of_end(struct px_state, nonce));
		SHA256_Final(hash, &sha256);
	} while (*(uint64_t*)&hash & ((1ull << px_state->difficulty)-1));
}

void *pixel_solver() {
	struct px_state *px_state;

	while (1) {
		int rd = solve_queue->rd;
		int new_rd = (rd + 1) % solve_queue->size;
		int px_offset;

		do {
			while (solve_queue->rd == solve_queue->wr) {
				syscall(__NR_futex, &solve_queue->wr,
					FUTEX_WAIT, solve_queue->rd), NULL;
			}

			solve_queue->rd = new_rd;
		} while (__atomic_compare_exchange_n(&solve_queue->rd, &rd, new_rd, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE));
		syscall(__NR_futex, &solve_queue->rd, FUTEX_WAKE, 2);

		px_state = &image_state[solve_queue->queue[new_rd]];

		px_offset = (px_state->x - x_offset + (px_state->y - y_offset) * width) * 4;

#if 1
		if (abs(image[px_offset] - px_state->r) < 10 &&
		    abs(image[px_offset + 1] - px_state->g) < 10 &&
		    abs(image[px_offset + 2] - px_state->b) < 10)
			continue;
#endif

		solve_pixel(px_state);
		px_state->new_r = image[px_offset];
		px_state->new_g = image[px_offset + 1];
		px_state->new_b = image[px_offset + 2];

		__atomic_add_fetch(&stat_px_solved, 1, __ATOMIC_ACQUIRE);

		//printf("SOLVED: %d, %d, %d\n", new_rd, px_state->x, px_state->y);
		write(set_sock, px_state, sizeof(*px_state));
	}
}

int open_socket()
{
	int sock;
	int buffer_size = 1024 * 1024; // 10 MiB

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		exit(EXIT_FAILURE);
	}

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(8080);
	if (inet_pton(AF_INET, "172.29.165.125", &server_addr.sin_addr) <= 0) {
		close(sock);
		exit(EXIT_FAILURE);
	}

	if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
		close(sock);
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) < 0) {
		close(sock);
		exit(EXIT_FAILURE);
	}
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) < 0) {
		close(sock);
		exit(EXIT_FAILURE);
	}

	return sock;
}

int main(int argc, const char **argv)
{
	int fd;

	width = atoi(argv[1]);
	height = atoi(argv[2]);

	if (argc >= 4) {
		x_offset = atoi(argv[3]);
		y_offset = atoi(argv[4]);
	}

	fd = open("/tmp/image", O_RDONLY);
	image = mmap(NULL, width * height * 4, PROT_READ, MAP_SHARED, fd, 0);

	image_state = calloc(width * height, sizeof(*image_state));

	request_sock = open_socket();
	set_sock = open_socket();

	solve_queue = malloc(sizeof(*solve_queue) + SOLVE_QUEUE_SIZE * sizeof(solve_queue->queue[0]));
	solve_queue->rd = 0;
	solve_queue->wr = 0;
	solve_queue->size = SOLVE_QUEUE_SIZE;

	pthread_create(&thread, NULL, pixel_requester, NULL);
	pthread_create(&thread, NULL, pixel_receiver, NULL);
	pthread_create(&thread, NULL, pixel_solver, NULL);
	pthread_create(&thread, NULL, pixel_solver, NULL);
	pthread_create(&thread, NULL, pixel_solver, NULL);
	pthread_create(&thread, NULL, pixel_solver, NULL);
	pthread_create(&thread, NULL, pixel_solver, NULL);
	pthread_create(&thread, NULL, pixel_solver, NULL);
	pthread_create(&thread, NULL, pixel_solver, NULL);
	pthread_create(&thread, NULL, pixel_solver, NULL);
	pthread_create(&thread, NULL, pixel_receiver_set, NULL);

	/* No need to clean up, let the kernel do it */
	return 0;
}