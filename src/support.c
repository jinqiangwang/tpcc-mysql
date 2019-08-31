/*
 * support.c
 * routines needed for the tpcc loading and transaction programs
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

// #include <sys/types.h>
// #include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#include "tpc.h"

static int nums[CUST_PER_DIST];
static int perm_count;

void SetSeed(int seed)
{
	srand(seed);
}

/*
 * return number uniformly distributed b/w min and max, inclusive
 */
int RandomNumber(int min, int max)
{
	return min + (rand() % ((max - min) + 1));
}

/*
 * non uniform random -- see p. 15
 *
 * the constant C depends on which value of A is passed, but the same
 * value of C should be used for all calls with the same value of
 * A.  however, we know in advance which values of A will be used.
 */
int NURand(unsigned A, unsigned x, unsigned y)
{
	static int first = 1;
	unsigned C, C_255, C_1023, C_8191;

	if (first)
	{
		C_255 = RandomNumber(0, 255);
		C_1023 = RandomNumber(0, 1023);
		C_8191 = RandomNumber(0, 8191);
		first = 0;
	}

	switch (A)
	{
	case 255:
		C = C_255;
		break;
	case 1023:
		C = C_1023;
		break;
	case 8191:
		C = C_8191;
		break;
	default:
		fprintf(stderr,
				"NURand: unexpected value (%d) of A used\n",
				A);
		abort();
	}

	return (int)(((RandomNumber(0, A) | RandomNumber(x, y)) + C) % (y - x + 1)) + x;
}


static size_t get_text_buf(char * path, char ** ppbuf) {
  const   size_t          max_alloc_len = 64 * 1024 * 1024 + 1;
  static  char            *text_buf     = NULL;
  static  size_t          text_len      = 0;
  static  pthread_mutex_t mutex         = PTHREAD_MUTEX_INITIALIZER;

  if (ppbuf == NULL) {
    printf("Invalid buffer porinter.\n");
    exit(-1);
  }
  
  pthread_mutex_lock(&mutex);
  if (NULL == text_buf)
  {
    text_buf = calloc(max_alloc_len, sizeof(char));
    int file = open(path, O_RDONLY);
    if (-1 == file)
    {
      printf("Failed to open file [%s] with error [%d].\n", path, errno);
      pthread_mutex_unlock(&mutex);
      exit(errno);
    }
    // read at most 64MB data from the specified file
    //
    text_len = read(file, text_buf, max_alloc_len - 1);
    if (0xffffffff == text_len)
    {
      printf("Failed to read file [%s] with error [%d].\n", path, errno);
      close(file);
      pthread_mutex_unlock(&mutex);
      exit(errno);
    }
    close(file);
  }
  pthread_mutex_unlock(&mutex);

  *ppbuf = text_buf;
  return text_len;
}

/*
	Fill data buffer from specfied text file.
	File path is passed in by "path"
*/
static int MakeAlphaStringFromFile(const *path, int x, int y, char str[])
{
	static __thread char *text_buf = NULL;
	static __thread size_t text_len = 0;
	static __thread size_t offset = 0;

	if (NULL == text_buf)
	{
		text_len = get_text_buf(path, &text_buf);
	}

	register int i =0, len = RandomNumber(x, y);
	for (; i < len; i++)
	{
		switch (text_buf[(offset + i) % text_len])
		{
		case '\\':
			str[i] = '_';
			break;
		case '\'':
			str[i] = '-';
			break;
		default:
			str[i] = text_buf[(offset + i) % text_len];
		}
	}

	offset = (offset + len) % text_len;
	return len;
}

static int MakeAlphaStringRandom(int x, int y, char str[])
{
	static char *alphanum = "0123456789"
							"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
							"abcdefghijklmnopqrstuvwxyz";
	int arrmax = 61; /* index of last array element */
	register int i, len;

	len = RandomNumber(x, y);

	for (i = 0; i < len; i++)
		str[i] = alphanum[RandomNumber(0, arrmax)];

	return len;
}

static char * src_file_path = NULL;

const char * GetSrcFilePath() {
	return src_file_path;
}

void SetSrcFilePath(const char * path_in) {
	if (NULL == path_in) {
		free(src_file_path);
		src_file_path = NULL;
	} else {
		if (path_in != src_file_path) {
			free(src_file_path);
			src_file_path = calloc(sizeof(char), strlen(path_in) + 1);
			strcpy(src_file_path, path_in);
		}
	}
}

/*
 * p. 54
 *
 * make a ``random a-string'': a string of random alphanumeric
 * characters of a random length of minimum x, maximum y, and
 * mean (y+x)/2
 */
int MakeAlphaString(int x, int y, char str[])
{
	if (NULL == src_file_path) {
		return MakeAlphaStringRandom(x, y, str);
	}

	return MakeAlphaStringFromFile(src_file_path, x, y, str);
}

/*
 * like MakeAlphaString, only numeric characters only
 */
int MakeNumberString(int x, int y, char str[])
{
	static char *numeric = "0123456789";
	int arrmax = 9;
	register int i, len;

	len = RandomNumber(x, y);

	for (i = 0; i < len; i++)
		str[i] = numeric[RandomNumber(0, arrmax)];

	return len;
}

/*
 * turn system time into database format
 * the format argument should be a strftime() format string that produces
 *   a datetime string acceptable to the database
 */
void gettimestamp(char str[], char *format, size_t len)
{
	time_t t;
	struct tm *datetime;

	t = time(NULL);
	datetime = localtime(&t);

	if (!strftime(str, len, format, datetime))
	{
		fprintf(stderr, "error writing timestamp to string\n");
		abort();
	}
}

/*
 * permute the list of customer ids for the order table
 */
void InitPermutation(void)
{
	int *cur;
	int i, j;

	perm_count = 0;

	/* initialize with consecutive values [1..ORD_PER_DIST] */
	for (i = 0, cur = nums; i < ORD_PER_DIST; i++, cur++)
	{
		*cur = i + 1;
	}

	/* now, shuffle */
	for (i = 0; i < ORD_PER_DIST - 1; i++)
	{
		j = (int)RandomNumber(i + 1, ORD_PER_DIST - 1);
		swap_int(nums[i], nums[j]);
	}
}

int GetPermutation(void)
{
	if (perm_count >= ORD_PER_DIST)
	{
		fprintf(stderr, "GetPermutation: past end of list!\n");
		abort();
	}
	return nums[perm_count++];
}

/*==================================================================+
 | ROUTINE NAME
 |      Lastname
 | DESCRIPTION
 |      TPC-C Lastname Function.
 | ARGUMENTS 
 |      num  - non-uniform random number
 |      name - last name string
 +==================================================================*/
void Lastname(num, name) int num;
char *name;
{
	static char *n[] =
		{"BAR", "OUGHT", "ABLE", "PRI", "PRES",
		 "ESE", "ANTI", "CALLY", "ATION", "EING"};

	strcpy(name, n[num / 100]);
	strcat(name, n[(num / 10) % 10]);
	strcat(name, n[num % 10]);

	return;
}
