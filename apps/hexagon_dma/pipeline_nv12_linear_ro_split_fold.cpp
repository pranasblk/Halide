#include "Halide.h"

using namespace Halide;

class DmaPipeline : public Generator<DmaPipeline> {
public:
    Input<Buffer<uint8_t>> input_y{"input_y", 2};
    Input<Buffer<uint8_t>> input_uv{"input_uv", 3};
    Output<Buffer<uint8_t>> output_y{"output_y", 2};
    Output<Buffer<uint8_t>> output_uv{"output_uv", 3};

    void generate() {
        Var x{"x"}, y{"y"}, c{"c"};

        // We need a wrapper for the output so we can schedule the
        // multiply update in tiles.
        Func copy_y("copy_y");
        Func copy_uv("copy_uv");
 
        copy_y(x, y) = input_y(x, y);
        copy_uv(x, y, c) = input_uv(x, y, c);

        output_y(x, y) = copy_y(x, y) * 2;
        output_uv(x, y, c) = copy_uv(x, y, c) * 2;

        Var tx("tx"), ty("ty");

        // Break the output into tiles.
        const int tile_width = 8;
        const int tile_height = 4;
  
        // tweak stride/extent to handle UV deinterleaving
        input_uv.dim(0).set_stride(2);
        input_uv.dim(2).set_stride(1).set_bounds(0, 2);
        output_uv.dim(0).set_stride(2);
        output_uv.dim(2).set_stride(1).set_bounds(0, 2);

        Expr fac = output_y.dim(1).extent()/2;
        Var yo, yi;
        output_y.split(y, yo, yi, fac);

        output_y
            .compute_root()
            .tile(x, yi, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
            .parallel(yo);

        Expr facx = output_uv.dim(1).extent()/2;
        Var yox, yix;
        output_uv.split(y, yox, yix, facx);

        output_uv
            .compute_root()
            .reorder(c, x, yox)   // to handle UV interleave, with 'c' inner most loop, as DMA'd into buffer
            .bound(c, 0, 2)
            .tile(x, yix, tx, ty, x, y, tile_width, tile_height, TailStrategy::RoundUp)
            .parallel(yox);

        // Schedule the copy to be computed at tiles with a
        // circular buffer of two tiles.
        copy_y
            .compute_at(output_y, tx)
            .store_at(output_y, ty)
            .copy_to_host().fold_storage(x, tile_width * 2);

        copy_uv
            .compute_at(output_uv, tx)
            .store_at(output_uv, ty)
            .bound(c, 0, 2)
            .copy_to_host()
            .reorder_storage(c, x, y)
            .fold_storage(x, tile_width * 2);
    }

};

HALIDE_REGISTER_GENERATOR(DmaPipeline, pipeline_nv12_linear_ro_split_fold)
