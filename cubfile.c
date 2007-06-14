/*
 * Library for accessing Cub files (files generated by the CUBIT finite
 * element meshing application.)
 *
 * Copyright 2006, Jason Kraftcheck (kraftche@cae.wisc.edu)
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 */

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include "cubfile.h"

/* Constants from CUBIT source */
uint32_t BIGENDIAN = 0xFFFFFFFF;
uint32_t LITENDIAN = 0x00000000;


static void print_error( int err, FILE* stream )
{
  static const char* const errors[] = { "OKAY", 
                                        "INVALID FILE", 
                                        "CORRUPT FILE", 
                                        "OVERFLOW",
                                        "NOT FOUND" };
  
  if (err > 0) {
    perror("");
    return;
  }
  
  err = -err;
  if (err >= (sizeof(errors)/sizeof(errors[0])))
    fprintf( stream, "UNKNOWN ERROR\n" );
  else
    fprintf( stream, "%s\n", errors[err] );
}

/* return cubit constant for endianness of this machine */
static uint32_t get_endian(void) {
  static const unsigned i = 1;
  static const unsigned char* const ptr = (const unsigned char*)&i;
  const uint32_t results[2] = { BIGENDIAN, LITENDIAN };
  return results[*ptr];
}

/* do byte swapping */
static void swap_bytes( void* bytes, size_t count, size_t width )
{
  unsigned char *start, *end, tmp;
  unsigned char *curptr = (unsigned char*)(bytes);
  unsigned char *endptr = curptr + (count*width);
  
  while( curptr < endptr ) {
    start = curptr;
    curptr += width;
    end = curptr - 1;

    while( end > start ) {
      tmp = *start;
      *start = *end;
      *end = tmp;
      
      start++;
      end--;
    }
  }
}

/* read initial file data */
static int check_file_internal( FILE* file, 
                                int* swap, 
                                int* count, 
                                unsigned long* offset )
{
  char buffer[4];
  uint32_t data[6];
  
  if (fseek( file, 0, SEEK_SET ))
    return errno;
  
  if (!fread( buffer, 4, 1, file))
    return errno;
  
  if (memcmp( buffer, "CUBE", 4 ))
    return CUB_FILE_INVALID;
  
  if (6 != fread( data, sizeof(uint32_t), 6, file))
    return errno;
  
  if (data[0] != BIGENDIAN && data[0] != LITENDIAN)
    return CUB_FILE_CORRUPT;
  
  *swap = (data[0] != get_endian());
  if (*swap)
    swap_bytes( data, 6, sizeof(uint32_t) );
  
  *count = data[2];
  *offset = data[3];
  return 0;
}

/* read initial file data */
int cub_file_check( FILE* file, int* swap, int* count )
{
  int swap_space, count_space;
  unsigned long offset;
  int result;
  
  result = check_file_internal( file, &swap_space, &count_space, &offset );
  if (result)
    return result;
  
  if (swap)
    *swap = swap_space;
  if (count)
    *count = count_space;
  return 0;
}

/* read table of contents */
int cub_file_contents( FILE* file, struct CubFileBlock* blocks, int blocks_length )
{
  int result, swap, count, i;
  unsigned long offset;
  uint32_t data[6];
  
  result = check_file_internal( file, &swap, &count, &offset );
  if (result)
    return result;
  
  if (blocks_length < count)
    return CUB_FILE_OVERFLOW;
  
  if (fseek( file, offset, SEEK_SET ))
    return errno;
  
  for (i = 0; i < count; ++i) {
    if (6 != fread( data, sizeof(uint32_t), 6, file))
      return errno;
    
    if (swap)
      swap_bytes( data, 6, sizeof(uint32_t) );
      
    blocks[i].type = (enum CubFileType)data[0];
    blocks[i].offset = data[1];
    blocks[i].length = data[2];
  }
  
  return 0;
}

/* write contents of file */
void cub_file_list( FILE* file, FILE* stream )
{
  static const char* const typenames[] = { "?", 
                                           "ACIS", 
                                           "MESH",
                                           "FACET",
                                           "FREE MESH",
                                           "GRANITE",
                                           "ASSEMBLY" };
  
  int result, count, i;
  struct CubFileBlock* data;
  
  result = cub_file_check( file, 0, &count );
  if (result) {
    print_error( result, stream );
    return;
  }
  
  if (!count) {
    fprintf(stream,"Table of contents is empty\n");
    return;
  }
  
  data = (struct CubFileBlock*)malloc( count * sizeof(struct CubFileBlock) );
  result = cub_file_contents( file, data, count );
  if (result) {
    free(data);
    print_error( result, stream );
    return;
  }
  
  fprintf(stream, "Idx  Type Name  Type      Offset      Length\n");
  fprintf(stream, "---  ---------  ----  ----------  ----------\n");
  for (i = 0; i < count; ++i) {
    fprintf(stream, "%3d  %9s  %4u  %10lu  %10lu\n", i,
      data[i].type <= CUB_FILE_ASSEMBLY ? typenames[data[i].type] : typenames[0],
      (unsigned)(data[i].type),
      (unsigned long)(data[i].offset),
      (unsigned long)(data[i].length) );
  }
}

static int internal_copy_data( FILE* infile, 
                               unsigned long offset,
                               unsigned long length,
                               FILE* outfile )
{
  unsigned char buffer[1024];
  size_t ww, count;
  unsigned char* ptr;

  if(fseek( infile, offset, SEEK_SET ))
    return errno;
  
  while (length) {
    count = length < sizeof(buffer) ? length : sizeof(buffer);
    count = fread( buffer, 1, count, infile );
    if (!count)
      return errno;
    length -= count;

    ptr = buffer;
    while (count) {
      ww = fwrite( ptr, 1, count, outfile );
      if (!ww)
        return errno;
      count -= ww;
      ptr += ww;
    }
  }
  
  return 0;
}

int cub_file_block( FILE* cubfile, FILE* outfile, int block )
{
  int result, count;
  struct CubFileBlock* data;
  
  result = cub_file_check( cubfile, 0, &count );
  if (result)
    return result;
  
  if (!count)
    return CUB_FILE_CORRUPT;
  
  data = (struct CubFileBlock*)malloc( count * sizeof(struct CubFileBlock) );
  result = cub_file_contents( cubfile, data, count );
  if (result) {
    free(data);
    return result;
  }
  
  if (block >= count || !data[block].length) {
    free(data);
    return CUB_FILE_NOT_FOUND;
  }
  
  result = internal_copy_data( cubfile, 
                               data[block].offset,
                               data[block].length,
                               outfile );
  free(data);
  return result;
}
  
int cub_file_type( FILE* cubfile, FILE* outfile, enum CubFileType type )
{
  int result, count, i;
  struct CubFileBlock* data;
  
  result = cub_file_check( cubfile, 0, &count );
  if (result)
    return result;
  
  if (!count)
    return CUB_FILE_CORRUPT;
  
  data = (struct CubFileBlock*)malloc( count * sizeof(struct CubFileBlock) );
  result = cub_file_contents( cubfile, data, count );
  if (result) {
    free(data);
    return result;
  }
  
  for (i = 0; i < count && data[i].type != type; ++i);
  if (i == count || !data[i].length) {
    free(data);
    return CUB_FILE_NOT_FOUND;
  }
  
  result = internal_copy_data( cubfile, 
                               data[i].offset,
                               data[i].length,
                               outfile );
  free(data);
  return result;
}
  
  

#ifdef TEST
int main( int argc, const char* argv[] )
{
  int i;
  FILE* file;
  
  for (i = 1; i < argc; ++i) {
    printf( "%s :\n", argv[i] );
    
    file = fopen( argv[i], "r" );
    if (file) {
      cub_file_list( file, stdout );
      fclose(file);
    }
    else {
      perror( argv[i] );
    }
  }
  
  return 0;
}
#endif

  
  
  
