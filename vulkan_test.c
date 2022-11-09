#include <stdlib.h>
#include <stdio.h>

#include <vulkan/vulkan.h>


#define LOG_ERROR(...) do { fprintf (stderr,   "VK HELPER error  : "__VA_ARGS__); fprintf(stderr, "\n"); } while(0)
#define LOG_WARNING(...) do { fprintf (stdout, "VK HELPER warning: "__VA_ARGS__); fprintf(stdout, "\n"); } while(0)
#define LOG_MESSAGE(...) do { fprintf (stdout, "VK HELPER message: "__VA_ARGS__); fprintf(stdout, "\n"); } while(0)
	
#define VULKAN_HELPER_FLAGS_ENABLE_VALIDATION_LAYERS_BIT	(0x1 << 0) /* enable validation layers enumerated in array below (requested_validation_layers) */
#define VULKAN_HELPER_FLAGS_ONE_QUEUE_FAMILY_BIT		(0x1 << 1) /* compute and transfer are the same family: DO NOT SET, THIS GETS SET AUTOMATICALLY */
#define VULKAN_HELPER_BUFFERS_ALLOCATED_BIT			(0x1 << 2) /* is set if the buffer allocation is ok: DO NOT SET, THIS GETS SET AUTOMATICALLY */

#define BUFFER_ELEMENTS 1024
	
const char *requested_extensions[] = 
{
	VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
};

const char *requested_validation_layers[] = {
	"VK_LAYER_KHRONOS_validation"
};

struct vk_helper_device_queues
{
	uint32_t compute_family;
	uint32_t transfer_family;
	uint32_t family_flags;
};

struct vulkan_helper
{
	int flags;
	VkInstance vk_instance;
	VkPhysicalDevice vk_physical_device;
	VkDevice vk_device;
	VkDebugUtilsMessengerEXT vk_debug_messenger;
	struct vk_helper_device_queues queues;
	VkQueue vk_queue_compute;
	VkQueue vk_queue_transfer;
	VkCommandPool vk_command_pool_compute, vk_command_pool_transfer;
	uint32_t *buffer_input, *buffer_output;
	size_t buffer_size;
	VkBuffer vk_buffer_device, vk_buffer_host;
	VkDeviceMemory vk_memory_device, vk_memory_host;
};


/* check if the provided extension is available */
/* WARNING: is thread safe only after returning once */
int
vulkan_helper_extension_available(const char * extension_name)
{
	static uint32_t extension_count = 0;
	static VkExtensionProperties *extensions = NULL;
	int i;
	if(extensions == NULL)
	{
		vkEnumerateInstanceExtensionProperties(NULL, &extension_count, NULL);
		extensions = malloc(extension_count * sizeof(VkExtensionProperties));
		vkEnumerateInstanceExtensionProperties(NULL, &extension_count, extensions);
	}
	
	for(i = 0; i < extension_count; i++)
	{
		if(strcmp(extension_name, (char *) extensions[i].extensionName) == 0)
		{
			return 1;
		}
	}
	
	return 0;
	
}

int
vulkan_helper_validation_layers_supported(void)
{
	int layer_count;
	int requested_layer_count = sizeof(requested_validation_layers) / sizeof(requested_validation_layers[0]);
	vkEnumerateInstanceLayerProperties(&layer_count, NULL);
	VkLayerProperties *available_layers = malloc(layer_count * sizeof(VkLayerProperties));
	vkEnumerateInstanceLayerProperties(&layer_count, available_layers);
	
	int i, j, found, all_found = 1;
	for(i = 0; i < requested_layer_count; i++)
	{
		found = 0;
		for(j = 0; j < layer_count; j++)
		{
			if(strcmp(requested_validation_layers[i], available_layers[j].layerName) == 0)
			{
				found = 1;
				break;
			}
		}
		
		if(!found)
		{
			LOG_WARNING("Unsupported required validation layer: %s", requested_validation_layers[i]);
			all_found = 0;
		}
	}
	free(available_layers);
	if(!all_found)
	{
		LOG_WARNING("Some validation layers missing. Skipping validation.");
	}
	return all_found;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
vulkan_helper_vk_debug_callback(
	VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
	VkDebugUtilsMessageTypeFlagsEXT message_type,
	const VkDebugUtilsMessengerCallbackDataEXT* p_callback_data,
	void* p_user_data)
{
	if(message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT || message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
	{
		LOG_MESSAGE("%s", p_callback_data -> pMessage);
	}
	else if(message_severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
	{
		LOG_WARNING("%s", p_callback_data -> pMessage);
	}
	else
	{
		LOG_ERROR("%s", p_callback_data -> pMessage);
	}
	
	return VK_FALSE;
}

VkResult
create_debug_utils_messenger_EXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* p_create_info, const VkAllocationCallbacks* p_allocator, VkDebugUtilsMessengerEXT* p_debug_messenger)
{
	PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != NULL)
	{
		return func(instance, p_create_info, p_allocator, p_debug_messenger);
	}
	else
	{
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

void
destroy_debug_utilsMessenger_EXT(VkInstance instance, VkDebugUtilsMessengerEXT debug_messenger, const VkAllocationCallbacks* p_allocator)
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != NULL)
    {
        func(instance, debug_messenger, p_allocator);
    }
}

int
vulkan_helper_vk_debug_messenger_init(struct vulkan_helper *vk_helper)
{
	if(!(vk_helper -> flags & VULKAN_HELPER_FLAGS_ENABLE_VALIDATION_LAYERS_BIT)) return 0;
	VkDebugUtilsMessengerCreateInfoEXT dmsg_create_info = {};
	dmsg_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	dmsg_create_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	dmsg_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	dmsg_create_info.pfnUserCallback = vulkan_helper_vk_debug_callback;
	dmsg_create_info.pUserData = NULL;
	
	if(create_debug_utils_messenger_EXT(vk_helper -> vk_instance, &dmsg_create_info, NULL, &(vk_helper -> vk_debug_messenger)) != VK_SUCCESS)
	{
		LOG_ERROR("Failed to create debug messenger");
		return 1;
	}
	return 0;
}

int
vulkan_helper_vk_debug_messenger_cleanup(struct vulkan_helper *vk_helper)
{
	if(!(vk_helper -> flags & VULKAN_HELPER_FLAGS_ENABLE_VALIDATION_LAYERS_BIT)) return 0;
	destroy_debug_utilsMessenger_EXT(vk_helper -> vk_instance, vk_helper -> vk_debug_messenger, NULL);
	
	return 0;
}


struct vk_helper_device_queues
vulkan_helper_find_queue_families(const VkPhysicalDevice p_dev)
{
	struct vk_helper_device_queues queues = {};
	
	uint32_t queue_family_count = 0, i;
	
	vkGetPhysicalDeviceQueueFamilyProperties(p_dev, &queue_family_count, NULL);
	VkQueueFamilyProperties *queue_families = malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
	vkGetPhysicalDeviceQueueFamilyProperties(p_dev, &queue_family_count, queue_families);
	
	for(i = 0; i < queue_family_count; i++)
	{
		if(!(queues.family_flags & VK_QUEUE_COMPUTE_BIT) && queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
		{
			queues.compute_family = i;
			queues.family_flags |= VK_QUEUE_COMPUTE_BIT;
		}
		
		if(!(queues.family_flags & VK_QUEUE_TRANSFER_BIT) && queue_families[i].queueFlags & VK_QUEUE_TRANSFER_BIT)
		{
			queues.transfer_family = i;
			queues.family_flags |= VK_QUEUE_TRANSFER_BIT;
		}
		
		
		if(queues.family_flags & VK_QUEUE_COMPUTE_BIT && queues.family_flags & VK_QUEUE_TRANSFER_BIT)
		{
			break;
		}
	}
	
	
	free(queue_families);
	
	
	return queues;
}

/* 
	generate an integer score for a given device, the bigger the score the better.
		negative score means we cant use that device
			-1 = device does not support compute or transfer queues
			-2 = device can not create compute work groups
		Currently rating the device for compute capabilities, not graphics
*/
int
vulkan_helper_rate_device(const VkPhysicalDevice p_dev)
{
	int rating = 1;
	VkPhysicalDeviceProperties device_properties;
	vkGetPhysicalDeviceProperties(p_dev, &device_properties);
/*	
	VkPhysicalDeviceFeatures device_features;
	vkGetPhysicalDeviceFeatures(p_dev, &device_features);
*/	
	
	
	struct vk_helper_device_queues queues = vulkan_helper_find_queue_families(p_dev);
	
	if(!((queues.family_flags & VK_QUEUE_COMPUTE_BIT) && (queues.family_flags & VK_QUEUE_TRANSFER_BIT)))
	{
		return -1;
	}

	if(!(device_properties.limits.maxComputeWorkGroupInvocations >= 1))
	{
		return -2;
	}
	
	if(device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
	{
		rating += 2 * 1024 * 1024;
	}
	
	rating += device_properties.limits.maxComputeSharedMemorySize;
	
	return rating;
}

int
vulkan_helper_device_init(struct vulkan_helper *vk_helper)
{
	int i;
	uint32_t device_count = 0;
	
	vkEnumeratePhysicalDevices(vk_helper -> vk_instance, &device_count, NULL);
	
	if(device_count == 0)
	{
		LOG_ERROR("Failed to find a GPU with Vulkan support");
		return -1;
	}
	
	VkPhysicalDevice *devices = malloc(device_count * sizeof(VkPhysicalDevice));
	vkEnumeratePhysicalDevices(vk_helper -> vk_instance, &device_count, devices);
	
	int rating = 0;
	vk_helper -> vk_physical_device = VK_NULL_HANDLE;
	
	for(i = 0; i < device_count; i++)
	{
		if(vulkan_helper_rate_device(devices[i]) > rating)
		{
			vk_helper -> vk_physical_device = devices[i];
		}
	}
	
	if(vk_helper -> vk_physical_device == VK_NULL_HANDLE)
	{
		LOG_ERROR("No sutible device found!");
		free(devices);
		return -1;
	}
	else
	{
		VkPhysicalDeviceProperties device_properties;
		vkGetPhysicalDeviceProperties(vk_helper -> vk_physical_device, &device_properties);
		LOG_MESSAGE("Selected sutible device: %s", device_properties.deviceName);
	}
	
	vk_helper -> queues = vulkan_helper_find_queue_families(vk_helper -> vk_physical_device);
	
	float queue_priorities[2] = {1.0f, 1.0f};
	VkDeviceQueueCreateInfo queue_create_infos[2] = {};
	queue_create_infos[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_infos[0].queueFamilyIndex = vk_helper -> queues.compute_family;
	queue_create_infos[0].queueCount = 1;
	queue_create_infos[0].pQueuePriorities = &queue_priorities[0];
	
	queue_create_infos[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_infos[1].queueFamilyIndex = vk_helper -> queues.transfer_family;
	queue_create_infos[1].queueCount = 1;
	queue_create_infos[1].pQueuePriorities = &queue_priorities[1];
	
	VkDeviceCreateInfo logical_device_create_info = {};
	logical_device_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	logical_device_create_info.pQueueCreateInfos = queue_create_infos;
	/* create only one queue if it supports both compue and transfer */
	int is_same_queue = vk_helper -> queues.compute_family == vk_helper -> queues.transfer_family;
	logical_device_create_info.queueCreateInfoCount = vk_helper -> is_same_queue ? 1 : 2;
	if(is_same_queue)
	{
		vk_helper -> flags |= VULKAN_HELPER_FLAGS_ONE_QUEUE_FAMILY_BIT;
	}
	
	logical_device_create_info.pEnabledFeatures = NULL;
	
	logical_device_create_info.enabledExtensionCount = sizeof(required_device_extensions) / sizeof(required_device_extensions[0]);
	logical_device_create_info.ppEnabledExtensionNames = required_device_extensions;
	
	if(vkCreateDevice(vk_helper -> vk_physical_device, &logical_device_create_info, NULL, &(vk_helper -> vk_device)) != VK_SUCCESS)
	{
		LOG_ERROR("Failed to create logical device");
		free(devices);
		return -1;
	}
	
	vkGetDeviceQueue(vk_helper -> vk_device,vk_helper -> queues.compute_family, 0, &(vk_helper -> vk_queue_compute));
	vkGetDeviceQueue(vk_helper -> vk_device, vk_helper -> queues.transfer_family, 0, &(vk_helper -> vk_queue_transfer));
	
	free(devices);
	return 0;
}

int
vulkan_helper_device_cleanup(struct vulkan_helper *vk_helper)
{
	vkDestroyDevice(vk_helper -> vk_device, NULL);
	return 0;
}

int
vulkan_helper_command_pools_init(struct vulkan_helper *vk_helper)
{
	VkCommandPoolCreateInfo command_pool_create_info = {};
	command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	command_pool_create_info.queueFamilyIndex = vk_helper -> vk_queue_compute;
	command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	vkCreateCommandPool(vk_helper -> vk_device, &command_pool_create_info, NULL, &(vk_helper -> vk_command_pool_compute));
	
	if(!(vk_helper -> flags & VULKAN_HELPER_FLAGS_ONE_QUEUE_FAMILY_BIT))
	{
		VkCommandPoolCreateInfo command_pool_create_info = {};
		command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		command_pool_create_info.queueFamilyIndex = vk_helper -> vk_queue_transfer;
		command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		vkCreateCommandPool(vk_helper -> vk_device, &command_pool_create_info, NULL, &(vk_helper -> vk_command_pool_transfer));
	}
	else
	{
		vk_helper -> vk_command_pool_transfer = vk_helper -> vk_command_pool_compute;
	}
	return 0;
}

int
vulkan_helper_command_pools_cleanup(struct vulkan_helper *vk_helper)
{
	vkDestroyCommandPool(vk_helper -> vk_device, vk_helper -> vk_command_pool_compute);
	if(!(vk_helper -> flags & VULKAN_HELPER_FLAGS_ONE_QUEUE_FAMILY_BIT))
	{
		vkDestroyCommandPool(vk_helper -> vk_device, vk_helper -> vk_command_pool_transfer);
	}
	return 0;
}

VkResult
vulkan_helper_create_buffer(struct vulkan_helper *vk_helper, VkBufferUsageFlags usage_flags, VkMemoryPropertyFlags memory_property_flags, VkBuffer *buffer, VkDeviceMemory *memory, VkDeviceSize size, void *data)
{
	VkBufferCreateInfo buffer_create_info = {}; 
	buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	buffer_create_info.usage = usage_flags;
	buffer_create_info.size = size;
	buffer_create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	vkCreateBuffer(vk_helper -> vk_device, &buffer_create_info, NULL, buffer);

	VkPhysicalDeviceMemoryProperties device_memory_properties; 
	vkGetPhysicalDeviceMemoryProperties(vk_helper -> vk_physical_device, &device_memory_properties);
	VkMemoryRequirements memory_requirements;
	VkMemoryAllocateInfo memory_allocation_info = {};
	memory_allocation_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	vkGetBufferMemoryRequirements(vk_helper -> vk_device, *buffer, &memory_requirements);
	memory_allocation_info.allocationSize = memory_requirements.size;


	int mem_type_found = 0;
	for (uint32_t i = 0; i < device_memory_properties.memoryTypeCount; i++)
	{
		if ((memory_requirements.memoryTypeBits & 1) == 1)
		{
			if ((device_memory_properties.memoryTypes[i].propertyFlags & memory_property_flags) == memory_property_flags)
			{
				memory_allocation_info.memoryTypeIndex = i;
				mem_type_found = 1;
			}
		}
		memory_requirements.memoryTypeBits >>= 1;
	}
	assert(mem_type_found);
	vkAllocateMemory(vk_helper -> vk_device, &memory_allocation_info, NULL, memory);

	if (data != NULL)
	{
		void *mapped;
		vkMapMemory(vk_helper -> vk_device, *memory, 0, size, 0, &mapped);
		memcpy(mapped, data, size);
		vkUnmapMemory(vk_helper -> vk_device, *memory);
	}

	vkBindBufferMemory(vk_helper -> vk_device, *buffer, *memory, 0);

	return VK_SUCCESS;
}


int
vulkan_helper_buffer_cleanup(struct vulkan_helper *vk_helper)
{
	if(!(vk_helper -> flags & VULKAN_HELPER_BUFFERS_ALLOCATED_BIT)) return 0;
	
	free(vk_helper -> buffer_output);
	free(vk_helper -> buffer_input);
	vkDestroyBuffer(vk_helper -> vk_device, vk_helper -> vk_buffer_device, NULL);
	vkFreeMemory(vk_helper -> vk_device, vk_helper -> vk_memory_device, NULL);
	vkDestroyBuffer(vk_helper -> vk_device, vk_helper -> vk_buffer_host, NULL);
	vkFreeMemory(vk_helper -> vk_device, vk_helper -> vk_memory_host, NULL);
	return 0;
}

int
vulkan_helper_init(struct vulkan_helper *vk_helper, int flags)
{
	int i;
	if((flags & VULKAN_HELPER_FLAGS_ONE_QUEUE_FAMILY_BIT) || (flags & VULKAN_HELPER_BUFFERS_ALLOCATED_BIT))
	{
		LOG_WARNING("Do not set VULKAN_HELPER_FLAGS_ONE_QUEUE_FAMILY_BIT flag, it is set by the device initializier!");
		flags &= ~(VULKAN_HELPER_FLAGS_ONE_QUEUE_FAMILY_BIT | VULKAN_HELPER_BUFFERS_ALLOCATED_BIT);
	}
	vk_helper -> flags = flags;
	VkApplicationInfo app_info = {};
	app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	app_info.pApplicationName = "Test application";
	app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.pEngineName = "Test Engine";
	app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	app_info.apiVersion = VK_API_VERSION_1_3;
	
	VkInstanceCreateInfo create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.pApplicationInfo = &app_info;
	
	
	/* SELECT EXTENSIONS */
	uint32_t requested_extensions_count = sizeof(requested_extensions) / sizeof(requested_extensions[0]);
	
	for(i = 0; i < requested_extensions_count; i++)
	{
		if(vulkan_helper_extension_available(requested_extensions[i]))
		{
			LOG_WARNING("Extension [%s] not available!", requested_extensions[i]);
		}
	}
	
	create_info.enabledExtensionCount = requested_extensions_count;
	create_info.ppEnabledExtensionNames = requested_extensions;
	create_info.enabledLayerCount = 0;
	if(flags & VULKAN_HELPER_FLAGS_ENABLE_VALIDATION_LAYERS_BIT && vulkan_helper_validation_layers_supported())
	{
		create_info.enabledLayerCount = sizeof(requested_validation_layers) / sizeof(requested_validation_layers[0]);
		create_info.ppEnabledLayerNames = requested_validation_layers;
	}
	
	VkResult result = vkCreateInstance(&create_info, NULL, &(vk_helper -> vk_instance));
	
	vulkan_helper_vk_debug_messenger_init(vk_helper);
	vulkan_helper_device_init(vk_helper);
	vulkan_helper_command_pools_init(vk_helper);
	
	return 0;
}

void
vulkan_helper_cleanup(struct vulkan_helper *vk_helper)
{
	vulkan_helper_buffer_cleanup(vk_helper);
	vulkan_helper_command_pools_cleanup(vk_helper);
	vulkan_helper_device_cleanup(vk_helper);
	vulkan_helper_vk_debug_messenger_cleanup(vk_helper);
	vkDestroyInstance(vk_helper -> vk_instance, NULL);
	return 0;
}


/*
	This function allocates two buffers, one host and one device. It performs a copy command on a transfer family queue command queue
	This command queue is used to test if the semaphore signals
	TODO(Dino): Most of the work should be done in this function
*/
int
vulkan_helper_test_semaphores_interop(struct vulkan_helper *vk_helper)
{
	int i;
	vk_helper -> buffer_size = BUFFER_ELEMENTS * sizeof(uint32_t);
	vk_helper -> buffer_input = (uint32_t *) malloc(vk_helper -> buffer_size);
	vk_helper -> buffer_output = (uint32_t *) malloc(vk_helper -> buffer_size);
	
	for(i = 0; i < BUFFER_ELEMENTS; i++)
	{
		vk_helper -> buffer_input[i] = i;
	}
	
	const VkDeviceSize device_buffer_size = vk_helper -> buffer_size;
	
	/* Copy input data to VRAM using a staging buffer */
	{
		vulkan_helper_create_buffer(
			vk_helper,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			&(vk_helper -> vk_buffer_host),
			&(vk_helper -> vk_memory_host),
			device_buffer_size,
			vk_helper -> buffer_input);

		void* mapped = NULL;
		vkMapMemory(vk_helper -> vk_device, vk_helper -> vk_memory_host, 0, VK_WHOLE_SIZE, 0, &mapped);
		VkMappedMemoryRange mapped_range = {};
		mapped_range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mapped_range.memory = vk_helper -> vk_memory_host;
		mapped_range.offset = 0;
		mapped_range.size = VK_WHOLE_SIZE;
		vkFlushMappedMemoryRanges(vk_helper -> vk_device, 1, &mapped_range);
		vkUnmapMemory(vk_helper -> vk_device, vk_helper -> vk_memory_host);

		vulkan_helper_create_buffer(
			vk_helper,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&(vk_helper -> vk_buffer_device),
			&(vk_helper -> vk_memory_device),
			device_buffer_size,
			NULL);

		vk_helper -> flags |= VULKAN_HELPER_BUFFERS_ALLOCATED_BIT;

		// Copy to staging buffer
		VkCommandBufferAllocateInfo command_buffer_allocate_info = {};
		command_buffer_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		command_buffer_allocate_info.commandPool = vk_helper -> vk_command_pool_transfer;
		command_buffer_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		command_buffer_allocate_info.commandBufferCount = 1;
		
		VkCommandBuffer copy_command_buffer;
		vkAllocateCommandBuffers(vk_helper -> vk_device, &command_buffer_allocate_info, &copy_command_buffer);
		
		
		VkCommandBufferBeginInfo command_buffer_begin_info = {};//vks::initializers::commandBufferBeginInfo();
		
		/* buffer recording */
		vkBeginCommandBuffer(copy_command_buffer, &command_buffer_begin_info);
		{
			VkBufferCopy copy_region = {};
			copy_region.size = device_buffer_size;
			vkCmdCopyBuffer(copy_command_buffer, vk_helper -> vk_buffer_host, vk_helper -> vk_buffer_device, 1, &copy_region);
		}
		vkEndCommandBuffer(copy_command_buffer);

		/* buffer submission */
		VkSubmitInfo submit_info = {};
		submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submit_info.commandBufferCount = 1;
		submit_info.pCommandBuffers = &copy_command_buffer;
		
		/* TODO(Dino): INSERT SEMAPHORES TO SIGNAL HERE*/
		/*
		submit_info.signalSemaphoreCount = NUMBER_OF_SEMAPHORES;
		submit_info.pSignalSemaphores = SEMAPHORES_TO_SIGNAL_POINTER
		*/
		
		
		VkFenceCreateInfo fence_create_info = {};
		fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fence_create_info.flags = VK_FLAGS_NONE;
		VkFence fence;
		vkCreateFence(vk_helper -> vk_device, &fence_create_info, NULL, &fence);

		// Submit to the queue
		vkQueueSubmit(queue, 1, &submit_info, fence);
		
		/* THIS FENCE WAITS UNTILL THE SUBMITET COMMAND QUEUE IS DONE */
		vkWaitForFences(vk_helper -> vk_device, 1, &fence, VK_TRUE, UINT64_MAX);
		/* TODO(Dino): INSERT CODE HERE TO CHECK IF THE INTEROP WORKS*/

		vkDestroyFence(vk_helper -> vk_device, fence, NULL);
		vkFreeCommandBuffers(vk_helper -> vk_device, commandPool, 1, &copy_command_buffer);
	}
	
	return 0;
}


int
main(int argc, char *argv[])
{
	struct vulkan_helper helper = {};
	vulkan_helper_init(&helper, VULKAN_HELPER_FLAGS_ENABLE_VALIDATION_LAYERS_BIT);
	
	vulkan_helper_test_semaphores_interop(&helper);
	
	vulkan_helper_cleanup(&helper);
	return 0;
}
