#ifndef BITMAP_H
#define BITMAP_H
namespace colorscreen
{
/* Constanly sized bitmap datastructure.  */
class bitmap
{
public:
  bitmap (size_t size1)
  {
    if (colorscreen_checking)
      assert (size1 >= 0);
    size = size1;
    data = (uint8_t *)calloc ((size + 7) / 8, 1);
  }
  ~bitmap () { free (data); }
  bool
  test_bit (size_t p)
  {
    if (colorscreen_checking)
      assert (p >= 0 && p < size);
    int pos = p / 8;
    int bit = p & 7;
    return data[pos] & (1U << bit);
  }
  bool
  set_bit (size_t p)
  {
    if (colorscreen_checking)
      assert (p >= 0 && p < size);
    int pos = p / 8;
    int bit = p & 7;
    bool ret = data[pos] & (1U << bit);
    data[pos] |= (1U << bit);
    return ret;
  }
  bool
  clear_bit (size_t p)
  {
    if (colorscreen_checking)
      assert (p >= 0 && p < size);
    int pos = p / 8;
    int bit = p & 7;
    bool ret = data[pos] & (1U << bit);
    data[pos] &= ~(1U << bit);
    return ret;
  }
  void
  clear ()
  {
    memset (data, 0, (size + 7) / 8);
  }

protected:
  uint8_t *data;
  size_t size;
};

/* Constanly sized 2d bitmap datastructure.  */
class bitmap_2d : private bitmap
{
public:
  bitmap_2d (int width1, int height1)
      : bitmap (width1 * height1), width (width1), height (height1)
  {
  }
  ~bitmap_2d () {}
  bool
  test_bit (int64_t x, int64_t y)
  {
    if (colorscreen_checking)
      assert (x >= 0 && y >= 0 && (size_t)x <= width && (size_t)y <= height);
    return bitmap::test_bit (y * width + x);
  }
  bool
  test_bit (int_point_t p)
  {
    if (colorscreen_checking)
      assert (p.x >= 0 && p.y >= 0 && (size_t)p.x <= width
              && (size_t)p.y <= height);
    return bitmap::test_bit (p.y * width + p.x);
  }
  bool
  set_bit (int64_t x, int64_t y)
  {
    if (colorscreen_checking)
      assert (x >= 0 && y >= 0 && (size_t)x <= width && (size_t)y <= height);
    return bitmap::set_bit (y * width + x);
  }
  bool
  set_bit (int_point_t p)
  {
    if (colorscreen_checking)
      assert (p.x >= 0 && p.y >= 0 && (size_t)p.x <= width
              && (size_t)p.y <= height);
    return bitmap::set_bit (p.y * width + p.x);
  }
  bool
  clear_bit (int64_t x, int64_t y)
  {
    if (colorscreen_checking)
      assert (x >= 0 && y >= 0 && (size_t)x <= width && (size_t)y <= height);
    return bitmap::clear_bit (y * width + x);
  }
  bool
  clear_bit (int_point_t p)
  {
    if (colorscreen_checking)
      assert (p.x >= 0 && p.y >= 0 && (size_t)p.x <= width && (size_t)p.y <= height);
    return bitmap::clear_bit (p.y * width + p.x);
  }
  bool
  test_range (int_point_t p, int r)
  {
    if (p.x < (int)r || p.y < (int)r || (size_t)p.x + r >= (size_t)width
        || (size_t)p.y + r >= (size_t)height)
      return false;
    for (size_t yy = p.y - r; yy < (size_t)(p.y + r); yy++)
      for (size_t xx = p.x - r; xx < (size_t)(p.x + r); xx++)
        if (!test_bit (xx, yy))
          return false;
    return true;
  }
  void
  clear ()
  {
    bitmap::clear ();
  }
  size_t width, height;
};
}
#endif
