
#ifndef _MESH_HPP_
#define _MESH_HPP_

#include <utility>

#include "nd_array/nd_array.hpp"

#include "constants.hpp"

template <int _ctrl_vols_x, int _ctrl_vols_y>
class[[nodiscard]] Mesh : public ND_Array<real, _ctrl_vols_x, _ctrl_vols_y> {
 public:
  static constexpr int ctrl_vols_x = _ctrl_vols_x;
  static constexpr int ctrl_vols_y = _ctrl_vols_y;

  using ControlVolumes = ND_Array<real, ctrl_vols_x, ctrl_vols_y>;

  [[nodiscard]] constexpr real x_min(int cell_x) const noexcept {
    return _x_min + cell_x * dx();
  }

  [[nodiscard]] constexpr real y_min(int cell_y) const noexcept {
    return _y_min + cell_y * dy();
  }

  [[nodiscard]] constexpr real x_max(int cell_x) const noexcept {
    return _x_min + (cell_x + 1) * dx();
  }

  [[nodiscard]] constexpr real y_max(int cell_y) const noexcept {
    return _y_min + (cell_y + 1) * dy();
  }

  [[nodiscard]] constexpr real x_median(int cell_x) const noexcept {
    return x_min(cell_x) + dx() / 2.0;
  }

  [[nodiscard]] constexpr real y_median(int cell_y) const noexcept {
    return y_min(cell_y) + dy() / 2.0;
  }

  [[nodiscard]] constexpr real dx() const noexcept { return _dx; }

  [[nodiscard]] constexpr real dy() const noexcept { return _dy; }

  [[nodiscard]] constexpr std::pair<int, int> cell_idx(
      const real x, const real y) const noexcept {
    return {static_cast<int>((x - x_min(0)) / dx()),
            static_cast<int>((y - y_min(0)) / dy())};
  }

  [[nodiscard]] constexpr real interpolate(const real x, const real y)
      const noexcept {
    // Use bilinear interpolation to approximate the value at x, y
    assert(x >= x_min(0));
    assert(x <= x_max(ctrl_vols_x - 1));
    assert(y >= y_min(0));
    assert(y <= y_max(ctrl_vols_y - 1));
    // Use an immediately invoked function expression
    // (beautiful C++ idiom names /sarcasm) to keep const and reduce scope
    // In theory, this will be as fast as the normal implementation,
    // though it's not always in practice
    //
    // This gets the cell indices which are to the right and/or above the cell
    // containing the point if those indices are valid
    // Otherwise, we use a bilinear approximation with cells to the left and/or
    // below the cell containing the point
    const auto [right, above] = [&]() {
      const real cell_right = x + dx() / 2.0;
      const real cell_above = y + dy() / 2.0;
      auto [i, j]           = cell_idx(cell_right, cell_above);
      if(i >= ctrl_vols_x) {
        i--;
      }
      if(j >= ctrl_vols_y) {
        j--;
      }
      assert(i >= 0);
      assert(j >= 0);
      return std::pair<int, int>({i, j});
    }();

    const real x_weight = (x - x_min(right)) / dx() * 2.0;
    const real y_weight = (y - y_min(above)) / dy() * 2.0;

    return x_weight * y_weight * this->value(right, above) +
           (1.0 - x_weight) * y_weight * this->value(right - 1.0, above) +
           x_weight * (1.0 - y_weight) * this->value(right, above - 1) +
           (1.0 - x_weight) * (1.0 - y_weight) *
               this->value(right - 1, above - 1);
  }

  constexpr Mesh(const real x_min, const real x_max, const real y_min,
                 const real y_max) noexcept
      : ND_Array<real, _ctrl_vols_x, _ctrl_vols_y>(),
        _x_min(x_min),
        _x_max(x_max),
        _y_min(y_min),
        _y_max(y_max),
        _dx((x_max - x_min) / ctrl_vols_x),
        _dy((y_max - y_min) / ctrl_vols_y) {
    for(int i = 0; i < this->extent(0); i++) {
      for(int j = 0; j < this->extent(1); j++) {
        this->value(i, j) = 0.0;
      }
    }
  }

  constexpr Mesh(const Mesh<ctrl_vols_x, ctrl_vols_y> &src) noexcept
		: Mesh(src._x_min, src._x_max, src._y_min, src._y_max) {}

 protected:
  const real _x_min, _x_max;
  const real _y_min, _y_max;
  const real _dx, _dy;
};

#endif  // _MESH_HPP_
