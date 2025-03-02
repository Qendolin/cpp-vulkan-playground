#include "Descriptors.h"

#include "Logger.h"

void DescriptorSetLayoutBase::validateBindings(std::span<const vk::DescriptorSetLayoutBinding> bindings) {
    for (uint32_t i = 0; i < bindings.size(); i++) {
        if (bindings[i].binding != i) {
            Logger::panic("Wrong descriptor binding index");
        }
    }
}
