#pragma once
#include "track_mesh.h"
#include "../app/track_mesh_capture.h"
#include "track_sampling_fixture.h"
#include <stdexcept>

namespace scr::test::mesh {
constexpr unsigned streamBase=0x7abda,pointerTable=0x7aa1a,endPointer=0x66102;
// Capture decoding is outside production code. Original generated-cache
// addresses are fixed by the hash-verified instructions at0x65bec/0x65c16.
inline TrackMeshData readData(const scr_test::ChipRam& r) {
    return viewer::decodeTrackMesh(r);
}
} // namespace scr::test::mesh
