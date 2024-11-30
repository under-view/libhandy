#define _GNU_SOURCE 1
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/stat.h>
#include <sys/mman.h>

#include "log.h"
#include "file-ops.h"

#define FILE_NAME_LEN_MAX (1<<12)


/*
 * @brief struct defining cando_file_ops instance
 *
 * @member fd       - File descriptor to open file.
 * @member pipefds  - File descriptors associated with an open pipe.
 * @member fname    - String representing the file name.
 * @member dataSize - Total size of the file.
 * @member data     - Pointer to mmap(2) file data.
 * @member retData  - Buffer of data that may be manipulated and
 *                    returned to caller.
 */
struct cando_file_ops
{
	int    fd;
	int    pipefds[2];
	char   fname[FILE_NAME_LEN_MAX];
	size_t dataSize;
	void   *data;
	void   *retData;
};


/********************************************
 * Start of cando_file_ops_create functions *
 ********************************************/

struct cando_file_ops *
cando_file_ops_create (const void *_fileCreateInfo)
{
	int ret = -1;

	struct stat fstats;

	struct cando_file_ops *flops = NULL;

	const struct cando_file_ops_create_info *fileCreateInfo = _fileCreateInfo;

	if (!fileCreateInfo) {
		return NULL;
	}

	flops = mmap(NULL,
		     sizeof(struct cando_file_ops),
		     PROT_READ|PROT_WRITE,
		     MAP_PRIVATE|MAP_ANONYMOUS,
		     -1, 0);
	if (flops == (void*)-1) {
		return NULL;
	}

	if (fileCreateInfo->fileName) {
		/* Check if file exist */
		ret = stat(fileCreateInfo->fileName, &fstats);

		memccpy(flops->fname, fileCreateInfo->fileName, '\n', FILE_NAME_LEN_MAX);

		flops->fd = open(flops->fname, O_CREAT|O_RDWR, 0644);
		if (flops->fd == -1) {
			cando_file_ops_destroy(flops);
			return NULL;
		}

		/*
		 * If file exists and caller defined dataSize set to 0.
		 * 	- Then set internal dataSize to equal the size of the file.
		 * 	- Else set internal dataSize to equal the caller defined size.
		 */
		flops->dataSize = (!ret && !(fileCreateInfo->dataSize)) ? \
				  (unsigned long int) fstats.st_size : \
				  fileCreateInfo->dataSize;
	}

	if (fileCreateInfo->createPipe) {
		ret = pipe(flops->pipefds);
		if (ret == -1) {
			cando_file_ops_destroy(flops);
			return NULL;
		}
	} else {
		ret = cando_file_ops_truncate_file(flops, flops->dataSize);
		if (ret < 0 && flops->dataSize) {
			cando_file_ops_destroy(flops);
			return NULL;
		}

		flops->data = mmap(NULL,
				   flops->dataSize,
				   PROT_READ,
				   MAP_PRIVATE,
				   flops->fd,
				   fileCreateInfo->offset);
		if (flops->data == (void*)-1 && flops->dataSize) {
			cando_file_ops_destroy(flops);
			return NULL;
		}

		flops->retData = mmap(NULL,
				      flops->dataSize,
				      PROT_READ,
				      MAP_PRIVATE|MAP_ANONYMOUS,
				      -1, 0);
		if (flops->retData == (void*)-1 && flops->dataSize) {
			cando_file_ops_destroy(flops);
			return NULL;
		}
	}

	ret = mprotect(flops, sizeof(struct cando_file_ops), PROT_READ);
	if (ret == -1) {
		cando_file_ops_destroy(flops);
		return NULL;
	}

	return flops;
}

/******************************************
 * End of cando_file_ops_create functions *
 ******************************************/


/***************************************************
 * Start of cando_file_ops_truncate_file functions *
 ***************************************************/

int
cando_file_ops_truncate_file (struct cando_file_ops *flops,
                              const unsigned long int dataSize)
{
	int ret = -1;

	if (!flops || dataSize == 0)
		return -1;

	ret = ftruncate64(flops->fd, dataSize);
	if (ret == -1) {
		return -errno;
	}

	return 0;
}

/*************************************************
 * End of cando_file_ops_truncate_file functions *
 *************************************************/


/*****************************************
 * Start of cando_file_ops_get functions *
 *****************************************/

const void *
cando_file_ops_get_data (struct cando_file_ops *flops,
                         const unsigned long int offset)
{
	if (!flops || \
	    !(flops->data) ||
	    offset >= flops->dataSize)
	{
		return NULL;
	}

	return ((char*)flops->data) + offset;
}


const char *
cando_file_ops_get_line (struct cando_file_ops *flops,
			 const unsigned long int lineNum)
{
	int ret = -1;

	unsigned long int offset, c, line = 0;

	if (!flops || \
            !(flops->data) || \
            !(flops->retData) || \
	    !lineNum)
	{
		return NULL;
	}

	for (offset = 0, c = 0; offset < flops->dataSize; offset++,c++) {
		if (*((char*) flops->data+offset) == '\n') {
			line++;

			if (line == lineNum) {
				break;
			} else {
				c = 0;
			}
		}
	}

	ret = mprotect(flops->retData, flops->dataSize, PROT_WRITE);
	if (ret == -1) {
		return NULL;
	}

	c -= (lineNum == 1) ? 0 : 1;
	memset(flops->retData, 0, c);
	memcpy(flops->retData, ((char*)flops->data)+(offset-c), c);
	*((char*)(flops->retData+c)) = '\0';

	ret = mprotect(flops->retData, flops->dataSize, PROT_READ);
	if (ret == -1) {
		return NULL;
	}

	return flops->retData;
}


long int
cando_file_ops_get_line_count (struct cando_file_ops *flops)
{
	long int offset, line = 0;

	if (!flops || \
            !(flops->data))
	{
		return -1;
	}

	for (offset = 0; offset < (long int) flops->dataSize; offset++)
		if (*((char*)flops->data + offset) == '\n')
			line++;

	return line;
}


int
cando_file_ops_get_fd (struct cando_file_ops *flops)
{
	if (!flops)
		return -1;

	return flops->fd;
}


const char *
cando_file_ops_get_filename (struct cando_file_ops *flops)
{
	if (!flops)
		return NULL;

	return flops->fname;
}

/***************************************
 * End of cando_file_ops_get functions *
 ***************************************/


/*****************************************
 * Start of cando_file_ops_set functions *
 *****************************************/

int
cando_file_ops_set_data (struct cando_file_ops *flops,
                         const void *_fileInfo)
{
	int ret = -1;

	const struct cando_file_ops_set_data_info *fileInfo = _fileInfo;

	if (!flops || \
            !(flops->data) || \
	    !fileInfo || \
	    !(fileInfo->data))
	{
		return -1;
	}

	ret = mprotect(flops->data, fileInfo->dataSize, PROT_WRITE);
	if (ret == -1) {
		return -1;
	}

	memcpy(((char*)flops->data)+fileInfo->offset, fileInfo->data, fileInfo->dataSize);

	ret = mprotect(flops->data, fileInfo->dataSize, PROT_READ);
	if (ret == -1) {
		return -1;
	}

	return 0;
}

/***************************************
 * End of cando_file_ops_set functions *
 ***************************************/


/*********************************************
 * Start of cando_file_ops_destroy functions *
 *********************************************/

void
cando_file_ops_destroy (struct cando_file_ops *flops)
{
	if (!flops)
		return;

	munmap(flops->data, flops->dataSize);
	munmap(flops->retData, flops->dataSize);
	close(flops->pipefds[0]);
	close(flops->pipefds[1]);
	close(flops->fd);
	munmap(flops, sizeof(struct cando_file_ops));
}

/*******************************************
 * End of cando_file_ops_destroy functions *
 *******************************************/
