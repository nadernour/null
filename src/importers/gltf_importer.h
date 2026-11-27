#pragma once

#include "core/model_data.h"
#include <string>

namespace ne {

namespace gltf_importer {

// iPath should be relative to the content directory.
ModelData importModel(const std::string& iPath);

} // namespace gltf_importer

} // namespace ne
