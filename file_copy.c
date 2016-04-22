#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <pthread.h>

#define STACK_SIZE 100
#define THREAD_LIM 10

DIR *stack[STACK_SIZE];
pthread_t th[THREAD_LIM];
char local_dir[PATH_MAX + 1] = "";
char input_dir[PATH_MAX + 1];
char output_dir[PATH_MAX + 1];

int thread_num;
int stack_pos = -1;

pthread_mutex_t mtx;

void concat_path(char *out, const char *dir, const char *name)
{
	if (strlen(dir) > 0) {
		strcpy(out, dir);
		strcat(out, "/");
		strcat(out, name);
	} else {
		strcpy(out, name);
	}
}

void cut_path(char *path)
{
	int i = strlen(path) - 1;

	while (i > 0 && '/' != path[i])
		--i;
	path[i] = '\0';
}

void go_from_dir()
{
	closedir(stack[stack_pos]);
	--stack_pos;
	cut_path(local_dir);
}

void go_to_dir(const char *name)
{
	char dir[PATH_MAX + 1];

	++stack_pos;
	if (stack_pos >= STACK_SIZE) {
		printf("stack overflow\n");
		exit(EXIT_FAILURE);
	}

	concat_path(local_dir, local_dir, name);

	concat_path(dir, input_dir, local_dir);
	stack[stack_pos] = opendir(dir);
	if (NULL == stack[stack_pos]) {
		printf("%s: %s\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
	}

	concat_path(dir, output_dir, local_dir);
	if (mkdir(dir, 0777)) {
		printf("%s: %s\n", dir, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void get_file_name(char *fname)
{
	struct dirent *de;
	pthread_mutex_lock(&mtx);
	do {
		errno = 0;
		de = readdir(stack[stack_pos]);
		if (NULL == de) {
			if (errno) {
				printf("%s/%s: %s\n", input_dir, local_dir,
						strerror(errno));
				exit(EXIT_FAILURE);
			}
			if (0 == stack_pos) {
				fname[0] = '\0';
				break;
			}

			go_from_dir();
		} else if (DT_REG == de->d_type) {
			concat_path(fname, local_dir, de->d_name);
			break;
		} else if (DT_DIR == de->d_type) {
			if (strcmp(de->d_name, ".") && strcmp(de->d_name, ".."))
				go_to_dir(de->d_name);
		}
	} while (1);
	pthread_mutex_unlock(&mtx);
}

void copy_file(const char *in_fname, const char *out_fname)
{
	FILE *fin, *fout;
	int n;
	char buff[1024];

	fin = fopen(in_fname, "rb");
	if (NULL == fin) {
		printf("%s: %s\n", in_fname, strerror(errno));
		exit(EXIT_FAILURE);
	}
	fout = fopen(out_fname, "wb");
	if (NULL == fout) {
		printf("%s: %s\n", out_fname, strerror(errno));
		exit(EXIT_FAILURE);
	}
	while ((n = fread(buff, 1, sizeof(buff), fin))) {
		if (fwrite(buff, 1, n, fout) != n) {
			printf("%s: %s\n", out_fname, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
	fclose(fin);
	fclose(fout);
}

void *worker(void *p)
{
	int id = (int)p;

	char fname[PATH_MAX + 1];
	char in_fname[PATH_MAX + 1];
	char out_fname[PATH_MAX + 1];

	get_file_name(fname);
	while (strlen(fname) > 0) {
		concat_path(in_fname, input_dir, fname);
		concat_path(out_fname, output_dir, fname);
		printf("[%d] %s\n", id, in_fname);
		copy_file(in_fname, out_fname);
		get_file_name(fname);
	}

	return 0;
}

void remove_right_slash(char *s)
{
	int i = strlen(s) - 1;

	while ('/' == s[i]) {
		s[i] = '\0';
		--i;
	}
}

int main(int argc, const char *argv[])
{
	int i;

	if (4 != argc) {
		printf("Usage: file_copy <threads number> <indir> <outdir>\n");
		return -1;
	}
	thread_num = atoi(argv[1]);
	if (0 == thread_num || THREAD_LIM < thread_num) {
		printf("invalid number of threads\n");
		exit(EXIT_FAILURE);
	}
	strcpy(input_dir, argv[2]);
	remove_right_slash(input_dir);
	strcpy(output_dir, argv[3]);
	remove_right_slash(output_dir);

	printf("number of threads: %d\n", thread_num);
	printf("input directory  : %s\n", input_dir);
	printf("output directory : %s\n", output_dir);

	++stack_pos;
	stack[stack_pos] = opendir(input_dir);
	if (NULL == stack[stack_pos]) {
		printf("%s: %s\n", input_dir, strerror(errno));
		exit(EXIT_FAILURE);
	}

	pthread_mutex_init(&mtx, NULL);

	for (i = 0; i < thread_num; ++i) {
		if (pthread_create(&th[i], NULL, worker, (void*)i)) {
			printf("%s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
	for (i = 0; i < thread_num; ++i)
		pthread_join(th[i], NULL);

	closedir(stack[stack_pos]);
	--stack_pos;

	return 0;
}
