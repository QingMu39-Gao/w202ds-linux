/* SPDX-License-Identifier: MIT */
/* Direct ICD loader smoke test. Zero devices is allowed only without --require-gpu. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

int main(int argc, char **argv)
{
   if (argc < 2 || argc > 3 ||
       (argc == 3 && strcmp(argv[2], "--require-gpu"))) {
      fprintf(stderr, "usage: %s <ICD-library> [--require-gpu]\n", argv[0]);
      return 2;
   }
   void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
   if (!library) {
      fprintf(stderr, "dlopen: %s\n", dlerror());
      return 1;
   }
   PFN_vkGetInstanceProcAddr get_proc =
      (PFN_vkGetInstanceProcAddr)dlsym(library, "vk_icdGetInstanceProcAddr");
   if (!get_proc) {
      fprintf(stderr, "missing vk_icdGetInstanceProcAddr\n");
      return 1;
   }
   PFN_vkCreateInstance create =
      (PFN_vkCreateInstance)get_proc(VK_NULL_HANDLE, "vkCreateInstance");
   if (!create)
      return 1;
   VkApplicationInfo app = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "panvk-icd-load",
      .apiVersion = VK_API_VERSION_1_1,
   };
   VkInstanceCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app,
   };
   VkInstance instance;
   VkResult result = create(&info, NULL, &instance);
   if (result != VK_SUCCESS) {
      fprintf(stderr, "vkCreateInstance: %d\n", result);
      return 1;
   }
   PFN_vkDestroyInstance destroy =
      (PFN_vkDestroyInstance)get_proc(instance, "vkDestroyInstance");
   PFN_vkEnumeratePhysicalDevices enumerate =
      (PFN_vkEnumeratePhysicalDevices)get_proc(instance, "vkEnumeratePhysicalDevices");
   PFN_vkGetPhysicalDeviceProperties properties =
      (PFN_vkGetPhysicalDeviceProperties)get_proc(instance, "vkGetPhysicalDeviceProperties");
   if (!destroy || !enumerate || !properties)
      return 1;
   uint32_t count = 0;
   result = enumerate(instance, &count, NULL);
   printf("vkEnumeratePhysicalDevices: %d, devices: %u\n", result, count);
   int status = result != VK_SUCCESS;
   if (!status && count) {
      VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
      if (!devices) {
         status = 1;
      } else {
         result = enumerate(instance, &count, devices);
         if (result != VK_SUCCESS) {
            status = 1;
         } else {
            for (uint32_t i = 0; i < count; i++) {
               VkPhysicalDeviceProperties props;
               properties(devices[i], &props);
               printf("device: %s, type: %u, vendor: %#x, device: %#x\n",
                      props.deviceName, props.deviceType,
                      props.vendorID, props.deviceID);
               if (argc == 3 && props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
                  status = 1;
            }
         }
         free(devices);
      }
   } else if (!count && argc == 3) {
      fprintf(stderr, "No GPU: this is not a successful hardware test\n");
      status = 1;
   }
   destroy(instance, NULL);
   dlclose(library);
   return status;
}
