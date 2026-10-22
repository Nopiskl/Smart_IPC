#include "snapshot_store.h"

namespace nopiskl {
Json SnapshotStore::put(const std::string &id, Image image) {
    if (image.bytes.size() > 8 * 1024 * 1024) {
        throw Error(507, "snapshot_too_large", "Snapshot exceeds 8 MiB");
    }
    Json metadata{{"snapshot_id", id}, {"mime", image.mime}, {"size", image.bytes.size()}};
    auto next = std::make_shared<const Image>(std::move(image));
    id_ = id;
    image_ = std::move(next);
    return metadata;
}
std::shared_ptr<const Image> SnapshotStore::get(const std::string &id) const {
    if (!image_ || id != id_) {
        throw Error(404, "snapshot_not_found", "Snapshot not found or evicted");
    }
    return image_;
}
} // namespace nopiskl
