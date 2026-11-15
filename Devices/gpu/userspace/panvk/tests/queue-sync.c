/* SPDX-License-Identifier: MIT */
/* Exercise the actual port's empty-submit fence helper with mocked ioctls. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int mock_ioctl(int fd, unsigned long request, ...);
#define ioctl mock_ioctl
#define drmSyncobjExportSyncFile mock_export
#define drmSyncobjImportSyncFile mock_import
#define _vk_queue_set_lost mock_queue_lost
#include PANVK_JM_QUEUE_SOURCE

static unsigned exports, merges, imports, lost;
static unsigned export_failure;
static bool merge_failure, import_failure;
static int allocated[32];
static unsigned allocated_count;

static int record_fd(int fd)
{
   assert(fd >= 0 && allocated_count < ARRAY_SIZE(allocated));
   allocated[allocated_count++] = fd;
   return fd;
}

int mock_export(int fd, uint32_t handle, int *out)
{
   assert(fd == 42 && handle != 0);
   if (++exports == export_failure) {
      errno = EIO;
      return -1;
   }
   *out = record_fd(eventfd(0, EFD_CLOEXEC));
   return 0;
}

int mock_import(int fd, uint32_t handle, int fence)
{
   assert(fd == 42 && handle == 99 && fcntl(fence, F_GETFD) >= 0);
   imports++;
   return import_failure ? -1 : 0;
}

VkResult mock_queue_lost(struct vk_queue *queue, const char *file, int line,
                         const char *message, ...)
{
   assert(queue && file && line > 0 && message);
   lost++;
   return VK_ERROR_DEVICE_LOST;
}

static int mock_ioctl(int fd, unsigned long request, ...)
{
   assert(request == SYNC_IOC_MERGE && fcntl(fd, F_GETFD) >= 0);
   va_list args;
   va_start(args, request);
   struct sync_merge_data *data = va_arg(args, struct sync_merge_data *);
   va_end(args);
   assert(fcntl(data->fd2, F_GETFD) >= 0);
   merges++;
   if (merge_failure) {
      errno = EIO;
      return -1;
   }
   data->fence = record_fd(dup(fd));
   return 0;
}

static void reset_case(void)
{
   for (unsigned i = 0; i < allocated_count; i++) {
      errno = 0;
      assert(fcntl(allocated[i], F_GETFD) == -1 && errno == EBADF);
   }
   allocated_count = exports = merges = imports = lost = 0;
   export_failure = 0;
   merge_failure = import_failure = false;
}

static void test_job_reuse(void)
{
   uint32_t job[pan_size(MALLOC_VERTEX_JOB) / 4] = {0};
   pan_pack((struct mali_job_header_packed *)job, JOB_HEADER, cfg) {
      cfg.type = MALI_JOB_TYPE_MALLOC_VERTEX;
      cfg.index = 7;
      cfg.next = 0x10000;
   }
   pan_section_pack(job, MALLOC_VERTEX_JOB, DRAW, cfg) {
      cfg.vertex_array.packet = true;
      cfg.minimum_z = 0.25f;
      cfg.maximum_z = 0.75f;
      cfg.shader.shader = 0x4000;
   }
   uint32_t original[ARRAY_SIZE(job)];
   memcpy(original, job, sizeof(job));
   for (unsigned i = 0; i < 1000; i++) {
      job[0] = 1;
      pan_section_unpack(job, MALLOC_VERTEX_JOB, DRAW, draw);
      draw.vertex_array.pointer = 0x2010000 + i * 64;
      draw.vertex_array.vertex_packet_stride = 64;
      draw.vertex_array.vertex_attribute_stride = 48;
      pan_section_pack(job, MALLOC_VERTEX_JOB, DRAW, cfg) { cfg = draw; }
      panvk_reset_job(job);
      assert(memcmp(job, original, sizeof(job)) == 0);
   }
   /* Fragment jobs have no vertex-array section: preserve their payload. */
   uint32_t fragment[pan_size(FRAGMENT_JOB) / 4];
   memset(fragment, 0x5a, sizeof(fragment));
   pan_pack((struct mali_job_header_packed *)fragment, JOB_HEADER, cfg) {
      cfg.type = MALI_JOB_TYPE_FRAGMENT;
      cfg.index = 3;
   }
   uint32_t expected[ARRAY_SIZE(fragment)];
   memcpy(expected, fragment, sizeof(fragment));
   fragment[0] = 1;
   panvk_reset_job(fragment);
   assert(memcmp(fragment, expected, sizeof(fragment)) == 0);
   puts("job-reuse: 1000 IDVS replays and fragment payload preservation passed");
}

int main(void)
{
   test_job_reuse();
   struct panvk_device dev = {0};
   struct panvk_gpu_queue queue = {0};
   dev.drm_fd = 42;
   queue.vk.base.device = &dev.vk;
   queue.sync = 99;
   const uint32_t waits[] = {99, 100, 101};

   assert(panvk_queue_merge_syncs(&queue, waits, 0) == VK_SUCCESS);
   assert(imports == 0);
   reset_case();
   assert(panvk_queue_merge_syncs(&queue, waits, 1) == VK_SUCCESS);
   assert(exports == 1 && merges == 0 && imports == 1 && lost == 0);
   reset_case();
   assert(panvk_queue_merge_syncs(&queue, waits, 3) == VK_SUCCESS);
   assert(exports == 3 && merges == 2 && imports == 1 && lost == 0);
   reset_case();
   export_failure = 2;
   assert(panvk_queue_merge_syncs(&queue, waits, 3) == VK_ERROR_DEVICE_LOST);
   assert(exports == 2 && imports == 0 && lost == 1);
   reset_case();
   merge_failure = true;
   assert(panvk_queue_merge_syncs(&queue, waits, 3) == VK_ERROR_DEVICE_LOST);
   assert(exports == 2 && merges == 1 && imports == 0 && lost == 1);
   reset_case();
   import_failure = true;
   assert(panvk_queue_merge_syncs(&queue, waits, 3) == VK_ERROR_DEVICE_LOST);
   assert(imports == 1 && lost == 1);
   reset_case();
   puts("queue-sync: 6 cases passed; exported/merged fds closed on every path");
   return 0;
}
