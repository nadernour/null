#pragma once

#include "core/model_data.h"
#include <string>

namespace ne {

namespace GltfImporter {

// iPath should be relative to the content directory.
ModelData importModel(const std::string& iPath);

} // namespace GltfImporter

} // namespace ne
