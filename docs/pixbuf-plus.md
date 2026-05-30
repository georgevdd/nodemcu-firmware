# Strided Slices of `pixbuf.buffer` Arrays

George van den Driessche, 2026

## Motivation

Programming complex patterns using the existing `pixbuf.buffer` API is possible, but high frame rates quickly become unachievable if a large number of lights are involved. This is largely because the cost of converting Lua parameters to C values is high. To set every third pixel (for example) in a buffer, the existing API requires calling `:set()` on each of those pixels individually. There is no way to make an object that refers to "every third pixel of this buffer" and then call `:set()` on that object.

Some `buffer` methods have begun to grow ways of addressing sub-buffers (e.g. `:shift()`) but this kind of support is ad-hoc, limited and inconsistent.

Existing techniques can be appropriated from other fields to give a rich, combinatoric vocabulary for describing efficient operations on subsets of pixels.

## Design

A series of steps introduce progressively richer vocabulary for manipulating `pixbuf.buffer` arrays, without substantially enlarging the buffer API or complicating its existing methods much.

To readers who know their way round the internals of NumPy or some other library for dealing with N-dimensional dense arrays, the following ideas are likely to look very familiar.

### Initial Situation

A `pixbuf.buffer` includes an array of byte values that are allocated contiguously with its header.
Its header fields describe the number of pixels and the channel count.
When the buffer is garbage-collected, its bytes are freed with it.

```
+-------+-----------+---+---+---+---+
| npix  | nchan     | values[] ...  |
+-------+-----------+---+---+---+---+
```

### First Step: Pointer to Values

  - Add a pointer to the header, to say where to find the values.
  - Update all existing code to use the pointer rather than to expect the values to lie directly after the header.
  
Usually the pointer does in fact point to the values directly after the header.

Now, though, a `pixbuf.buffer` can instead refer to a different `buffer`'s values. The two buffers share a single
values array. Modifications to one buffer object will be reflected in the other. Only one of the buffers
(the "base" buffer) owns the values array; the other acts as a view of the values.

It is important that a buffer which "lends" its values array to another buffer is not garbage-collected
before the borrower. Otherwise, the borrower will be left with a dangling pointer to a deallocated values array. So the header fields must also include a `base_ref` field. This is a Lua reference number, obtained from the Lua state, that acts just like a Lua reference to an object. As long as such a reference exists,
the referee will not be garbage collected.

So there are two possible setups now:

  - A `buffer` owns its values: its `base_ref` is nil and its `values_ptr` points immediately beyond its own header;
  - Or the `buffer` borrows its values: its `base_ref` identifies a different `buffer`, and its `values_ptr` points at that different `buffer`'s values. The two `buffer`s have the same `npix` and `nchan`.

```
+-------+-----------+--------------+---------------+---+---+---+---+
| npix  | nchan     | base_ref = 0 | values_ptr ---> values[] ...  |
+-------+-----------+--------------+---------------^---+---+---+---+
^                                                 /
|                                                /
`-----------------------------.                 |
+-------+-----------+----------|---+------------|--+
| npix  | nchan     | base_ref '   | values_ptr '  |
+-------+-----------+--------------+---------------+
```

Example:
```Lua
view = buffer:slice()
view:fill(0,0,0,0)  -- Modifies `buffer`
```

### Second Step: Pointer to Slice

  - Instead of pointing at another `pixbuf.buffer`'s values array, a `values_ptr` can point _into_ that array.
  - Only the `slice()` method needs changing; all other `buffer` code will continue to work as is.
  - `npix` must be decreased accordingly, to avoid the possibility of reading or writing beyond the array.

Such a view will look like some of the first values in the array have been skipped. `npix` can also be decreased further, to give a view where some of the _last_ values in the array have been skipped.

This gives everything necessary to talk about slices of buffers.

```
+-------+-----------+--------------+---------------+---+---+---+---+
| npix  | nchan     | base_ref = 0 | values_ptr ---> values[] ...  |
+-------+-----------+--------------+---------------+---+---^---+---+
^                                                          |
|                                                ,---------'
`-----------------------------.                 |
+-------+-----------+----------|---+------------|--+
| npix' | nchan     | base_ref '   | values_ptr '  |
+-------+-----------+--------------+---------------+
```

Example:
```Lua
view = buffer:slice(3, -3)  -- View from the third pixel to the third-from-last
view:fill(20,20,20,20)  -- Sets all of `buffer` except the first two and last two pixels
```

### Third Step: Pointer to Strided Slice

  - Introduce a `stride` field, describing the distance in bytes between pixels in the values array.
  - Update every `pixbuf.buffer` method that reads or writes pixels, to find pixels a distance of `stride` bytes apart instead of assuming they are `nchan` bytes apart.

For a base buffer that owns its values, the stride will necessarily be the same as the buffer's `nchan`,
because the values are contiguously allocated at the same time as the buffer.

For a `pixbuf.buffer` that is a slice, though, the stride can be a multiple of its base buffer's stride,
creating a view of every Nth pixel. Reading and writing the pixels of such a slice will read and
write every Nth pixel of the base buffer. The multiple can be negative, so that the elements
are indexed in reverse order.


```
                                                 .----------.
                                                |            |
                                                |            v
+-------+-----------+--------------+------------|--+---------+---+---+---+---+
| npix  | nchan     | base_ref = 0 | values_ptr '  | stride  | values[] ...  |
+-------+-----------+--------------+---------------+---------+---+---^---+---+
^                                                                    |
|                                                ,------------------'
`-----------------------------.                 |
+-------+-----------+----------|---+------------|--+---------+
| npix' | nchan     | base_ref '   | values_ptr '  | stride' |
+-------+-----------+--------------+---------------+---------+
```

Example:
```Lua
view = buffer:slice(-3, 3, -1)  -- View from the third-from-last pixel back to the third
view:set(1, 30, 30, 30, 30)  -- Set the third-from-last pixel of `buffer`
```

### Fourth Step: Pointer to Single Channel

  - Introduce a `channel()` method that creates a slice with `nchan = 1`.
  - No other changes are needed.

A single-channel slice has the same `stride` as its base buffer. Its `values_ptr` does not have to point to the first byte of a pixel in the base buffer, though. For example, if the base buffer holds GRBW values for pixels, then incrementing `values_ptr` by two will point it at successive pixels' B components.

This gives everything necessary to refer to just one colour channel of a `pixbuf.buffer`.

```
                                                 .----------.
                                                |            |
                                                |            v
+-------+-----------+--------------+------------|--+---------+---+---+---+---+
| npix  | nchan     | base_ref = 0 | values_ptr '  | stride  | values[] ...  |
+-------+-----------+--------------+---------------+---------+---+-^-+---+---+
^                                                                  |
|                                                ,----------------'
`-----------------------------.                 |
+-------+-----------+----------|---+------------|--+---------+
| npix' | nchan = 1 | base_ref '   | values_ptr '  | stride' |
+-------+-----------+--------------+---------------+---------+
```

Example:
```Lua
view = buffer:channel(3)  -- View the B values of a GRBW buffer
view:fill(50)  -- Set all the B values of the base buffer
```

## Limitations

Concatenation of `pixbuf.buffer` instances still requires copying them both into
a newly allocated result.

## Testing

All of the operations that are applicable to a base `pixbuf.buffer` buffer are
valid for any slice buffer too. So it makes sense to factor a library out
of the existing `pixbuf.buffer` test suite and reuse it for testing slices.

## Performance Considerations

Performance of base `pixbuf.buffer` instances should not be damaged by the
introduction of slices. Introducing strides to the `buffer` data structure
could be a problem, because it means that target pixels may not be
contiguous and uses of `memcpy()` must be replaced by a function that is
aware of strides. If benchmarks show that strided copies over contiguous
data are slower than `memcpy()` then the base buffer case (where
`stride == nchan` and the pixels are in fact contiguous) can be detected
and treated as a special case using `memcpy()` as before.
