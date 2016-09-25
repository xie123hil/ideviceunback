
/*
 * MIT licence
 *
 Copyright (c) 2016, Paul L Daniels
 All rights reserved.

 Redistribution and use in source and binary forms, with or  
 without modification, are permitted provided that the 
 following conditions are met:

 * Redistributions of source code must retain the above 
 copyright notice, this list of conditions and the following 
 disclaimer.

 * Redistributions in binary form must reproduce the above 
 copyright notice, this list of conditions and the following 
 disclaimer in the documentation and/or other materials provided 
 with the distribution.

 * Neither the name of the Paul L Daniels nor the names of its contributors 
 may be used to endorse or promote products derived from this software 
 without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS 
 FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE 
 COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, 
 BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN  
 ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE 
 POSSIBILITY OF SUCH DAMAGE.
 */ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <openssl/sha.h>

#define VERSION "1.00"
#define TOOLS_BLOCK_READ_BUFFER_SIZE 4096

int verbose =  0;

struct manrec {
	unsigned char hash[SHA_DIGEST_LENGTH];
	char hashstr[1024];
	char domain[1024];
	char filepath[1024];
	char abspath[1024];
	char digest[1024]; 
	char enckey[1024];
	char shain[2048];
	uint16_t mode;
	uint64_t inode;
	uint32_t userid;
	uint32_t groupid;
	uint32_t mtime;
	uint32_t atime;
	uint32_t ctime;
	uint64_t filelen;
	uint8_t flags;
	uint8_t numprops;
};

int filecopy( char *source, char *dest )
{
	char *buffer;
	FILE *s, *d;
	size_t rsize, wsize;

	s = fopen(source, "r");
	if (!s)
	{
		fprintf(stderr,"ERROR: Cannot open '%s' for reading (%s).", source, strerror(errno) );
		return -1;
	}

	d = fopen(dest, "w");
	if (!d)
	{
		fprintf(stderr,"ERROR: Cannot open '%s' for writing (%s).", dest, strerror(errno) );
		return -1;
	}

	buffer = malloc( TOOLS_BLOCK_READ_BUFFER_SIZE );
	if (!buffer)
	{
		fprintf(stderr,"ERROR: Cannot allocate %d bytes for buffer", TOOLS_BLOCK_READ_BUFFER_SIZE );
		return -1;
	}

	do {
		rsize = fread( buffer, 1, TOOLS_BLOCK_READ_BUFFER_SIZE, s );
		if (rsize > 0)
		{
			wsize = fwrite( buffer, 1, rsize, d );
			if ( rsize != wsize )
			{
				fprintf(stderr,"WARNING: Read '%lu' bytes, but only coule write '%lu'", rsize, wsize );
			}
		}
	} while ( rsize > 0 );

	fclose(s);
	fclose(d);

	free(buffer);

	return 0;
}



int mkdirp( char *path, int mode )
{
	int result = 0;
	char c = '/';
	char *p = path;

	if (*p == '/') p++;

	while ((p != NULL)&&(*p != '\0'))
	{
		struct stat st;
		int stat_result;

		p = strchr(p,'/');
		if (p != NULL)
		{
			while (*(p+1) == '/') p++;
			c = *p;
			*p = '\0';
		}

		stat_result = stat(path, &st);
		if ((stat_result == 0)&&(S_ISDIR(st.st_mode)||S_ISLNK(st.st_mode)))
		{
			// If the link is good, then do nothing
		} else if (stat_result == -1) {
			int mkresult=0;

			mkresult = mkdir(path,mode);
			if (mkresult != 0)
			{
				fprintf(stderr,"ERROR: while attempting mkdir('%s'); '%s'",path,strerror(errno));
				return -1;
			}
		} else {
			fprintf(stderr,"ERROR: path %s seems to already exist as a non-directory",path);
			return -1;
		}

		if (p != NULL)
		{
			*p = c; p++;
		}

	}

	return result;
}

char *splitpath( char *fullpath ) {
	char *p;

	if (fullpath) {
		p = strrchr(fullpath, '/');
		if (p) {
			*p = '\0';
			p++;
			return p;
		}
	}
	return NULL;
}

int readstr( char **p, char *buf ) {
	static unsigned short mask[] = {192, 224, 240};
	uint8_t i;
	uint8_t slr[2];
	uint16_t sl;

	slr[0] = **p;
	slr[1] = *((*p)+1);
	if ((slr[0] == 0xff)&&(slr[1] == 0xff)) {
		(*p)+=2;
		*buf = '\0';
		return 0;
	}
	sl = (slr[0] << 8) + slr[1];
	(*p)+=2;

	while ((**p != '\0')&&(sl--)) {
		i = 0;
		if ((**p & mask[0]) == mask[0]) i++;
		if ((**p & mask[1]) == mask[1]) i++;
		if ((**p & mask[2]) == mask[2]) i++;

		*buf = **p; buf++; 
		(*p)+=(i+1);
	}
	*buf = '\0';
	return 0;
}

int readuint8( char **p, uint8_t *i ) {
	*i = **p;

	(*p)++;
	return 0;
}

int readuint16( char **p, uint16_t *i ) {
	uint16_t a = 0;

	memcpy(&a, *p, sizeof(uint16_t));
	*i = (((a & 0x00FF) <<  8) | ((a & 0xFF00) >>  8));
	(*p) += 2;
	return 0;
}

int readuint32( char **p, uint32_t *i ) {
	uint32_t a = 0;

	memcpy(&a, *p, sizeof(uint32_t));
	*i = (
			((a & 0x000000FFUL) << 24) | 
			((a & 0x0000FF00UL) <<  8) | 
			((a & 0x00FF0000UL) >>  8) | 
			((a & 0xFF000000UL) >> 24) 
		 );
	(*p) += 4;

	return 0;
}

int readuint64( char **p, uint64_t *i ) {

	uint64_t a = 0;

	memcpy(&a, *p, sizeof(uint64_t));
	*i = ((a & 0x00000000000000FFULL) << 56) | 
		((a & 0x000000000000FF00ULL) << 40) | 
		((a & 0x0000000000FF0000ULL) << 24) | 
		((a & 0x00000000FF000000ULL) <<  8) | 
		((a & 0x000000FF00000000ULL) >>  8) | 
		((a & 0x0000FF0000000000ULL) >> 24) | 
		((a & 0x00FF000000000000ULL) >> 40) | 
		((a & 0xFF00000000000000ULL) >> 56);
	(*p) += 8;
	return 0;
}



int main( int argc, char **argv ) {

	struct manrec m;

	int i;
	char *addr, *p, *ep;
	int fd;
	struct stat sb;

	fd = open(argv[1], O_RDONLY);
	if (fd == -1)
		perror("open");

	if (fstat(fd, &sb) == -1)           /* To obtain file size */
		perror("fstat");

	addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		return 0;
	}

	p = addr;
	ep = addr +sb.st_size;
	p = addr +6; // jump the header
	while ( p < ep ) {

		readstr(&p, m.domain); // domain
		readstr(&p, m.filepath); // filepath
		readstr(&p, m.abspath); // absolute path for symlinks
		readstr(&p, m.digest); // digest
		readstr(&p, m.enckey); // encryption key

		if (verbose) {
			fprintf(stdout, "%s|%s|%s|%s|%s"
					, m.domain
					, m.filepath
					, m.abspath
					, m.digest
					, m.enckey
				   );
		}


		readuint16(&p, &m.mode); // mode
		readuint64(&p, &m.inode); // inode#
		readuint32(&p, &m.userid); // uid
		readuint32(&p, &m.groupid); // gid
		readuint32(&p, &m.mtime); // last modified time
		readuint32(&p, &m.atime); // last accessed time
		readuint32(&p, &m.ctime); // created time
		readuint64(&p, &m.filelen); // size
		readuint8(&p, &m.flags); // protection class
		readuint8(&p, &m.numprops); // number of properties

		if (verbose) {
			fprintf(stdout,"|%c%c%c"
					, m.mode & 0x4 ? 'r' : '-'
					, m.mode & 0x2 ? 'w' : '-'
					, m.mode & 0x1 ? 'x' : '-'
				   );

			fprintf(stdout,"|%lu|uid:%u gid:%u|Times(%u,%u,%u)|Size:%ld bytes|Flags:%02x|Numprops:%u"
					, m.inode
					, m.userid
					, m.groupid
					, m.mtime
					, m.atime
					, m.ctime
					, m.filelen
					, m.flags
					, m.numprops
				   );
		}


		if (m.numprops) {
			if (verbose > 1) fprintf(stdout,"\n");
			for (i = 0 ; i < m.numprops; i++) {
				char s1[1024], s2[1024];
				readstr(&p, s1);
				readstr(&p, s2);
				if (verbose > 1) fprintf(stdout,"\t%s=%s\n",s1,s2);
			}
		}


		snprintf(m.shain, sizeof(m.shain),"%s-%s", m.domain, m.filepath);
		SHA1((unsigned char*)m.shain, strlen(m.shain), m.hash);

		for (i=0; i < SHA_DIGEST_LENGTH; i++) {
			sprintf(m.hashstr+(i*2),"%02hhx",m.hash[i]);
		}

		{
			FILE *f;

			f = fopen(m.hashstr, "r");
			if (f) {
				char newpath[1024];
				char *fn;
				fprintf(stdout,"%s =(exists)=> %s", m.hashstr, m.filepath);
				fclose(f);
				snprintf(newpath, sizeof(newpath),"_unback_/%s", m.filepath);
				fn = splitpath(newpath);
				if (fn) {
					mkdirp( newpath, S_IRWXU );
					*(fn -1) = '/';
					filecopy( m.hashstr, newpath);
					fprintf(stdout, " copied");
				}
				fprintf(stdout,"\n");
			}
		}
	}



	munmap(addr, sb.st_size);
	close(fd);

	return 0;

}