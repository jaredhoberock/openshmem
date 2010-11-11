#include <stdio.h>
#include <assert.h>
#include <sys/types.h>

#include "state.h"
#include "comms.h"
#include "warn.h"
#include "shmem.h"

#include "memalloc.h"

long malloc_error = SHMEM_MALLOC_OK; /* exposed for error codes */


/*
 * check that all PEs see the same shmalloc size: return first
 * mis-matching PE id if there's a mis-match, return -1 to record
 * correct symmetry
 */

static long shmalloc_remote_size;

static int
shmalloc_symmetry_check(size_t size)
{
  int pe;
  int any_failed_pe = -1;
  long shmalloc_received_size;

  /* record for everyone else to see */
  shmalloc_remote_size = size;
  shmem_barrier_all();

  /* everyone checks everyone else's sizes, barf if mis-match */
  for (pe = 0; pe < __state.numpes; pe += 1) {
    if (pe == __state.mype) {
      continue;
    }
    shmalloc_received_size = shmem_long_g(&shmalloc_remote_size, pe);
    if (shmalloc_received_size != size) {
      __shmem_warn(SHMEM_LOG_NOTICE,
		   "shmalloc expected %ld, but saw %ld on PE %d",
		   size, shmalloc_received_size, pe
		   );
      malloc_error = SHMEM_MALLOC_SYMMSIZE_FAILED;
      any_failed_pe = pe;
      break;
    }
  }
  return any_failed_pe;
}

void *
shmalloc(size_t size)
{
  void *addr;

  if (shmalloc_symmetry_check(size) != -1) {
    malloc_error = SHMEM_MALLOC_SYMMSIZE_FAILED;
    return (void *) NULL;
  }

  __shmem_warn(SHMEM_LOG_DEBUG,
	       "shmalloc(%ld) passed symmetry check",
	       size
	       );

  addr = __mem_alloc(size);

  if (addr == (void *) NULL) {
    __shmem_warn(SHMEM_LOG_NOTICE,
		 "shmalloc(%ld) failed",
		 size
		 );
    malloc_error = SHMEM_MALLOC_FAIL;
  }
  else {
    malloc_error = SHMEM_MALLOC_OK;
  }

  __shmem_warn(SHMEM_LOG_DEBUG,
	       "shmalloc(%ld) @ %p",
	       size, addr
	       );

  shmem_barrier_all();		/* so say the SGI docs */

  return addr;
}
_Pragma("weak shmem_malloc=shmalloc")

void
shfree(void *addr)
{
  if (addr == (void *) NULL) {
    __shmem_warn(SHMEM_LOG_NOTICE,
		 "address passed to shfree() already null"
		 );
    malloc_error = SHMEM_MALLOC_ALREADY_FREE;
    return;
  }

  __shmem_warn(SHMEM_LOG_DEBUG,
	       "shfree(%p) in pool @ %p",
	       addr, __mem_base()
	       );

  __mem_free(addr);

  malloc_error = SHMEM_MALLOC_OK;

  shmem_barrier_all();
}
_Pragma("weak shmem_free=shfree")

void *
shrealloc(void *addr, size_t size)
{
  void *newaddr;

  if (shmalloc_symmetry_check(size) != -1) {
    malloc_error = SHMEM_MALLOC_SYMMSIZE_FAILED;
    return (void *) NULL;
  }

  if (addr == (void *) NULL) {
    __shmem_warn(SHMEM_LOG_NOTICE,
		 "address passed to shrealloc() already null"
		 );
    malloc_error = SHMEM_MALLOC_ALREADY_FREE;
    return (void *) NULL;
  }

  newaddr = __mem_realloc(addr, size);

  if (newaddr == (void *) NULL) {
    __shmem_warn(SHMEM_LOG_NOTICE,
		 "shrealloc() couldn't reallocate %ld bytes @ %p",
		 size, addr
		 );
    malloc_error = SHMEM_MALLOC_REALLOC_FAILED;
  }
  else {
    malloc_error = SHMEM_MALLOC_OK;
  }

  shmem_barrier_all();

  return newaddr;
}
_Pragma("weak shmem_realloc=shrealloc")

/*
 * The shmemalign function allocates a block in the symmetric heap that
 * has a byte alignment specified by the alignment argument.
 */
void *
shmemalign(size_t alignment, size_t size)
{
  void *addr;

  if (shmalloc_symmetry_check(size) != -1) {
    malloc_error = SHMEM_MALLOC_SYMMSIZE_FAILED;
    return (void *) NULL;
  }

  addr = __mem_align(alignment, size);

  if (addr == (void *) NULL) {
    __shmem_warn(SHMEM_LOG_NOTICE,
		 "shmem_memalign() couldn't resize %ld bytes to alignment %ld",
		 size, alignment
		 );
    malloc_error = SHMEM_MALLOC_MEMALIGN_FAILED;
  }
  else {
    malloc_error = SHMEM_MALLOC_OK;
  }

  shmem_barrier_all();

  return addr;
}
_Pragma("weak shmem_memalign=shmemalign")

/*
 * readable error message for error code "e"
 */
char *
sherror(void)
{
  switch (malloc_error) {
  case SHMEM_MALLOC_OK:
    return "no symmetric memory allocation error";
    break;
  case SHMEM_MALLOC_FAIL:
    return "symmetric memory allocation failed";
    break;
  case SHMEM_MALLOC_ALREADY_FREE:
    return "attempt to free already null symmetric memory address";
    break;
  case SHMEM_MALLOC_MEMALIGN_FAILED:
    return "attempt to align symmetric memory address failed";
    break;
  case SHMEM_MALLOC_REALLOC_FAILED:
    return "attempt to reallocate symmetric memory address failed";
    break;
  case SHMEM_MALLOC_SYMMSIZE_FAILED:
    return "asymmetric sizes passed to symmetric memory allocator";
    break;
  default:
    return "unknown error";
    break;
  }
}
_Pragma("weak shmem_error=sherror")
