/* Copy-on-write vector wrapper.
   Copyright (C) 2026 Jan Hubicka
   This file is part of ColorScreen.  */

#ifndef COW_VECTOR_H
#define COW_VECTOR_H

#include <vector>
#include <memory>
#include <algorithm>

namespace colorscreen
{

/* COW_VECTOR is a simple wrapper around std::vector that implements
   copy-on-write semantics using std::shared_ptr.  It allows cheap
   copying of large vectors until they are modified.  */
template <typename T>
class cow_vector
{
  std::shared_ptr<std::vector<T>> data;

public:
  /* Construct an empty vector.  */
  cow_vector ()
    : data (std::make_shared<std::vector<T>> ())
  {
  }

  /* Copy constructor and assignment.  */
  cow_vector (const cow_vector &other) = default;
  cow_vector &operator= (const cow_vector &other) = default;

  /* Assignment from std::vector.  */
  cow_vector &
  operator= (const std::vector<T> &v)
  {
    data = std::make_shared<std::vector<T>> (v);
    return *this;
  }

  /* Return a const reference to the underlying vector for reading.  */
  const std::vector<T> &
  read () const
  {
    return *data;
  }

  /* Implicit conversion to const reference to the underlying vector.  */
  operator const std::vector<T> & () const
  {
    return read ();
  }

  /* Return a non-const reference to the underlying vector for writing.
     If the vector is shared with other cow_vector instances, a deep
     copy is performed first.  */
  std::vector<T> &
  write ()
  {
    if (data.use_count () > 1)
      data = std::make_shared<std::vector<T>> (*data);
    return *data;
  }

  /* Return the number of elements.  */
  size_t
  size () const
  {
    return data->size ();
  }

  /* Return true if the vector is empty.  */
  bool
  empty () const
  {
    return data->empty ();
  }

  /* Return a const reference to the element at index I.  */
  const T &
  operator[] (size_t i) const
  {
    return (*data)[i];
  }

  /* Return a non-const reference to the element at index I.
     Triggers COW if shared.  */
  T &
  operator[] (size_t i)
  {
    return write ()[i];
  }

  /* Standard iterator support.  */
  typename std::vector<T>::const_iterator
  begin () const
  {
    return data->begin ();
  }

  typename std::vector<T>::const_iterator
  end () const
  {
    return data->end ();
  }

  /* Non-const iterators trigger COW.  */
  typename std::vector<T>::iterator
  begin ()
  {
    return write ().begin ();
  }

  typename std::vector<T>::iterator
  end ()
  {
    return write ().end ();
  }

  /* Add an element to the end of the vector.  */
  void
  push_back (const T &val)
  {
    write ().push_back (val);
  }

  void
  push_back (T &&val)
  {
    write ().push_back (std::move (val));
  }

  /* Clear the vector.  */
  void
  clear ()
  {
    if (data.use_count () == 1)
      data->clear ();
    else
      data = std::make_shared<std::vector<T>> ();
  }

  /* Erase element at position IT.  */
  void
  erase (typename std::vector<T>::iterator it)
  {
    /* If we need to copy, the iterator IT (which must come from this
       vector) will be invalidated.  We must convert it to index.  */
    size_t index = std::distance (data->begin (), it);
    write ().erase (write ().begin () + index);
  }

  /* Comparison operators.  */
  bool
  operator== (const cow_vector &other) const
  {
    if (data == other.data)
      return true;
    return *data == *other.data;
  }

  bool
  operator!= (const cow_vector &other) const
  {
    return !(*this == other);
  }

  /* Expose the underlying pointer for debugging or unique ownership checks.  */
  const T*
  raw_data () const
  {
    return data->data ();
  }
};

} // namespace colorscreen

#endif
