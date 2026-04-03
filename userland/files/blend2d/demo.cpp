#include <blend2d/blend2d.h>
#include <stdio.h>

int main() {
  printf("Entered main()\n");
  BLImage img(480, 320, BL_FORMAT_PRGB32);
  BLContext ctx(img);

  ctx.clear_all();
  ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx.fill_all(BLRgba32(0xFF10141A));

  BLGradient linear(BLLinearGradientValues(0, 0, 480, 320));
  linear.add_stop(0.0, BLRgba32(0xFF7AD7FF));
  linear.add_stop(0.5, BLRgba32(0xFF4E7CFF));
  linear.add_stop(1.0, BLRgba32(0xFF201B44));
  ctx.fill_round_rect(24.0, 24.0, 432.0, 272.0, 28.0, 28.0, linear);

  BLPath path;
  path.move_to(64, 88);
  path.cubic_to(220, 10, 330, 40, 404, 156);
  path.cubic_to(360, 292, 184, 288, 72, 200);
  path.close();

  ctx.set_fill_style(BLRgba32(0xEFFFFFFF));
  ctx.fill_path(path);
  ctx.set_stroke_style(BLRgba32(0xFF0B0F14));
  ctx.set_stroke_width(6.0);
  ctx.stroke_path(path);
  printf("Just before ctx.end();\n");
  ctx.end();
  printf("Done. Disk writing...\n");

  BLResult result = img.write_to_file("/mnt/drv0/blend2d-demo.png");
  if (result != BL_SUCCESS) {
    printf("blend2d-demo: write failed (%u)\n", result);
    return 1;
  }

  printf("Wrote /mnt/drv0/blend2d-demo.png\n");
  return 0;
}
