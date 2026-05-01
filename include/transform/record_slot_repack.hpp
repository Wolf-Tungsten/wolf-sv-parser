#ifndef WOLVRIX_TRANSFORM_RECORD_SLOT_REPACK_HPP
#define WOLVRIX_TRANSFORM_RECORD_SLOT_REPACK_HPP

#include "core/transform.hpp"

namespace wolvrix::lib::transform
{

    class RecordSlotRepackPass : public Pass
    {
    public:
        RecordSlotRepackPass();

        PassResult run() override;
    };

} // namespace wolvrix::lib::transform

#endif // WOLVRIX_TRANSFORM_RECORD_SLOT_REPACK_HPP
