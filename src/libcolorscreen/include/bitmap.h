#ifndef BITMAP_H
#define BITMAP_H
class bitmap
{
public:
  bitmap(size_t size1)
  {
    size = size1;
    data = (uint8_t *)calloc ((size + 7) / 8, 1);
  }
  ~bitmap()
  {
    free (data);
  }
  bool
  test_bit (size_t p)
  {
    int pos = p / 8;
    int bit = p & 7;
    return data[pos] & (1U << bit);
  }
  bool
  set_bit (size_t p)
  {
    int pos = p / 8;
    int bit = p & 7;
    bool ret = data[pos] & (1U << bit);
    data[pos] |= (1U << bit);
    return ret;
  }
  bool
  clear_bit (size_t p)
  {
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
  int size;
};

class bitmap_2d: private bitmap
{
public:
  bitmap_2d(int width1, int height1)
  : bitmap (width1 * height1), width (width1), height (height1)
  {
  }
  ~bitmap_2d()
  {
  }
  bool
  test_bit (size_t x, size_t y)
  {
    return bitmap::test_bit (y * width + x);
  }
  bool
  set_bit (size_t x, size_t y)
  {
    return bitmap::set_bit (y * width + x);
  }
  bool
  clear_bit (size_t x, size_t y)
  {
    return bitmap::clear_bit (y * width + x);
  }
  void
  clear ()
  {
    bitmap::clear ();
  }
  int width, height;
};
#endif
