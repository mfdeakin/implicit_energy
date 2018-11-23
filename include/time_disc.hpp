
#ifndef _TIME_DISC_HPP_
#define _TIME_DISC_HPP_

#include "constants.hpp"
#include "thomas.hpp"

#include <algorithm>
#include <cassert>
#include <memory>

template <typename _Mesh, typename _SpaceAssembly>
class Base_Solver {
 public:
  using MeshT         = _Mesh;
  using SpaceAssembly = _SpaceAssembly;

  Base_Solver(const real T_0 = 1.0, const real u_0 = 1.0, const real v_0 = 1.0,
              const real x_min = 0.0, const real x_max = 1.0,
              const real y_min = 0.0, const real y_max = 1.0)
      : _cur_mesh(std::make_unique<MeshT>(x_min, x_max, y_min, y_max)),
        _space_assembly(T_0, u_0, v_0),
        _time(0.0) {
    for(int i = 0; i < _cur_mesh->x_dim(); i++) {
      for(int j = 0; j < _cur_mesh->y_dim(); j++) {
        const real x = _cur_mesh->x_median(i);
        const real y = _cur_mesh->y_median(j);

        _cur_mesh->Temp(i, j)  = _space_assembly.solution(x, y, 0.0);
        _cur_mesh->u_vel(i, j) = _space_assembly.u(x, y);
        _cur_mesh->v_vel(i, j) = _space_assembly.v(x, y);
      }
    }
  }

  [[nodiscard]] constexpr real time() const noexcept { return _time; }

  [[nodiscard]] constexpr MeshT &mesh() const noexcept { return *_cur_mesh; }

  [[nodiscard]] constexpr SpaceAssembly &space_assembly() noexcept {
    return _space_assembly;
  }

 protected:
  std::unique_ptr<MeshT> _cur_mesh;
  SpaceAssembly _space_assembly;
  real _time;
};

template <typename _Mesh, typename _SpaceAssembly>
class ImplicitEuler_Solver : public Base_Solver<_Mesh, _SpaceAssembly> {
 public:
  using MeshT         = _Mesh;
  using SpaceAssembly = _SpaceAssembly;

  ImplicitEuler_Solver(const real T_0 = 1.0, const real u_0 = 1.0,
                       const real v_0 = 1.0, const real x_min = 0.0,
                       const real x_max = 1.0, const real y_min = 0.0,
                       const real y_max = 1.0)
      : Base_Solver<_Mesh, _SpaceAssembly>(T_0, u_0, v_0, x_min, x_max, y_min,
                                           y_max) {}

  void timestep(const real dt) {
    // First we need to construct our vector to use with the Thomas algorithm
    using VecMeshX  = ND_Array<real, MeshT::x_dim() + 2, MeshT::y_dim()>;
    using VecMeshXT = ND_Array<real, VecMeshX::extent(1), VecMeshX::extent(0)>;

    constexpr int vec_dim = VecMeshX::extent(0) * VecMeshX::extent(1);
    using SolVec          = ND_Array<real, vec_dim>;

    using Mtx      = ND_Array<real, vec_dim, 3>;
    using MtxMesh  = ND_Array<real, VecMeshX::extent(0), VecMeshX::extent(1), 3>;
    using MtxMeshT = ND_Array<real, VecMeshX::extent(1), VecMeshX::extent(0), 3>;

    const MeshT &mesh                   = *this->_cur_mesh;
    const SpaceAssembly &space_assembly = this->space_assembly();

    // Fill Mtx with our Dx Terms
    // Fill SolVec with the source terms and the flux integral
    // Then solve for the solution to our y vector
    // Then fill Mtx with our Dy Terms
    // And solve for our dT vector
    SolVec sol_dx;
		VecMeshX &sol_dx_mesh = sol_dx.template reshape<VecMeshX>();

    Mtx dx;
    MtxMeshT &dx_mesh = dx.template reshape<MtxMeshT>();

    // Enforce our boundary conditions
    for(int j = 0; j < MeshT::y_dim(); j++) {
      sol_dx_mesh(0, j)                  = 0.0;
      sol_dx_mesh(MeshT::x_dim() + 1, j) = 0.0;

      dx_mesh(j, 0, 0) = 0.0;
      dx_mesh(j, 0, 1) = 1.0;
      dx_mesh(j, 0, 2) = 1.0;

      dx_mesh(j, MtxMeshT::extent(1) - 1, 0) = 1.0;
      dx_mesh(j, MtxMeshT::extent(1) - 1, 1) = 1.0;
      dx_mesh(j, MtxMeshT::extent(1) - 1, 2) = 0.0;
    }
    for(int i = 0; i < MeshT::x_dim(); i++) {
      for(int j = 0; j < MeshT::y_dim(); j++) {
        sol_dx_mesh(i + 1, j) =
            (space_assembly.flux_integral(mesh, i, j, this->time()) +
             space_assembly.source_fd(mesh, i, j, this->time())) *
            dt;
        assert(!std::isnan(sol_dx_mesh(i + 1, j)));
        // (-u_{i - 1, j} / (2.0 \Delta x) - 1.0 / (Re * Pr * \Delta x^2))
        // \delta T_{i - 1, j} \Delta t

        // (1.0 + 2.0 / (Re * Pr) * (\Delta x^{-2} + \Delta y^{-2}) \Delta t)
        // \delta T_{i, j}

        // (u_{i + 1, j} / (2.0 \Delta x) - 1.0 / (Re * Pr * \Delta x^2)) \delta
        // T_{i + 1, j} \Delta t
        dx_mesh(j + 1, i, 0) = space_assembly.Dx_m1(mesh, i + 1, j) * dt;
        dx_mesh(j + 1, i, 1) = space_assembly.Dx_0(mesh, i + 1, j) * dt + 1.0;
        dx_mesh(j + 1, i, 2) = space_assembly.Dx_p1(mesh, i + 1, j) * dt;
      }
    }
    solve_thomas(dx, sol_dx);

    VecMeshXT transpose_sol;

    Mtx dy;
    MtxMesh &dy_mesh = dy.template reshape<MtxMesh>();
    // Enforce our boundary conditions
    for(int i = 0; i < MeshT::x_dim(); i++) {
      dy_mesh(i, 0, 0) = 0.0;
      dy_mesh(i, 0, 1) = 1.0;
      dy_mesh(i, 0, 2) = 1.0;

      dy_mesh(i, MtxMesh::extent(0) - 1, 0) = 1.0;
      dy_mesh(i, MtxMesh::extent(0) - 1, 1) = 1.0;
      dy_mesh(i, MtxMesh::extent(0) - 1, 2) = 0.0;
    }

    for(int i = 0; i < MeshT::x_dim(); i++) {
      for(int j = 0; j < MeshT::y_dim(); j++) {
        // Note that since the Thomas algorithm solves a tridiagonal system,
        // we need to swap some rows of the solution to make the matrix
        // tridiagonal ie, go from increasing the x index to increasing the y
        // index. This is easiest to achieve by taking the transpose of the
        // solution when looking at it like a matrix
        assert(!std::isnan(sol_dx_mesh(i, j)));
        transpose_sol(j, i) = sol_dx_mesh(i, j);
      }
    }

    for(int i = 0, m = 0; i < MeshT::x_dim(); i++) {
      for(int j = 0; j < MeshT::y_dim(); j++, m++) {
        // These terms come from the following equation:
        // (-v_{i, j - 1} / (2.0 \Delta y) - 1.0 / (Re * Pr * \Delta y^2))
        // \delta T_{i, j - 1} \Delta t (1.0 + 2.0 / (Re * Pr) * (\Delta x^{-2}
        // + \Delta y^{-2}) \Delta t) \delta T_{i, j} (v_{i, j + 1} / (2.0
        // \Delta y) - 1.0 / (Re * Pr * \Delta y^2)) \delta T_{i, j + 1} \Delta
        // t
        dy(m, 0) = space_assembly.Dy_m1(mesh, i, j) * dt;
        dy(m, 1) = space_assembly.Dy_0(mesh, i, j) * dt + 1.0;
        dy(m, 2) = space_assembly.Dy_p1(mesh, i, j) * dt;
        assert(!std::isnan(dy(m, 0)));
        assert(!std::isnan(dy(m, 1)));
        assert(!std::isnan(dy(m, 2)));
      }
    }
    SolVec &sol_dy = transpose_sol.template reshape<SolVec>();
    solve_thomas(dy, sol_dy);

    // sol now contains our dT terms
    // So just add it to our T terms
    // Recall that our sol_mesh is transposed, so we need to swap the indices
    // used for it
    for(int i = 0, m = 0; i < MeshT::x_dim(); i++) {
      for(int j = 0; j < MeshT::y_dim(); j++, m++) {
        assert(!std::isnan(transpose_sol(j, i)));
        this->_cur_mesh->Temp(i, j) -= transpose_sol(j, i);
      }
    }

    this->_time += dt;
  }
};

template <typename _Mesh, typename _SpaceAssembly>
class RK1_Solver : public Base_Solver<_Mesh, _SpaceAssembly> {
 public:
  using MeshT         = _Mesh;
  using SpaceAssembly = _SpaceAssembly;

  RK1_Solver(const real T_0 = 1.0, const real u_0 = 1.0, const real v_0 = 1.0,
             const real x_min = 0.0, const real x_max = 1.0,
             const real y_min = 0.0, const real y_max = 1.0)
      : Base_Solver<_Mesh, _SpaceAssembly>(T_0, u_0, v_0, x_min, x_max, y_min,
                                           y_max),
        _partial_mesh(std::make_unique<MeshT>(x_min, x_max, y_min, y_max)) {}

  void timestep(const real sigma_ratio) {
    static bool once = false;
    auto &mesh       = *(this->_cur_mesh);
    // |sigma| = |1 + \lambda \Delta t|
    // Approximate \lambda with just the second derivative term,
    // since it will dominate with small \Delta x.
    // Then |\sigma| \leq 1 + (4 / (Re * Pr)(\Delta t / \Delta x^2) = 1
    // So our maximum timestep is \Delta x^2 * Re * Pr / 4

    const real dt =
        sigma_ratio * mesh.dx() * mesh.dx() * reynolds * prandtl / 4.0;

    if(!once) {
      printf("RK1 dt: % .6e\n", dt);
      once = true;
    }

    this->_space_assembly.flux_assembly(*(this->_cur_mesh), *(this->_cur_mesh),
                                        *_partial_mesh, this->time(), dt);

    std::swap(this->_cur_mesh, _partial_mesh);
    this->_time += dt;
  }

 protected:
  std::unique_ptr<MeshT> _partial_mesh;
};

template <typename _Mesh, typename _SpaceAssembly>
class RK4_Solver : public Base_Solver<_Mesh, _SpaceAssembly> {
 public:
  using MeshT         = _Mesh;
  using SpaceAssembly = _SpaceAssembly;

  RK4_Solver(const real T_0 = 1.0, const real u_0 = 1.0, const real v_0 = 1.0,
             const real x_min = 0.0, const real x_max = 1.0,
             const real y_min = 0.0, const real y_max = 1.0)
      : Base_Solver<_Mesh, _SpaceAssembly>(T_0, u_0, v_0, x_min, x_max, y_min,
                                           y_max),
        _partial_mesh_1(std::make_unique<MeshT>(x_min, x_max, y_min, y_max)),
        _partial_mesh_2(std::make_unique<MeshT>(x_min, x_max, y_min, y_max)) {}

  void timestep(const real sigma_ratio) {
    auto &mesh = *(this->_cur_mesh);
    // Approximate the amplification factor as
    // 4^3 (\Delta t / \Delta x^2)^4 / (6 * Re * Pr) = sigma_mag
    // where sigma_mag = sigma_ratio^4
    // We can make this approximation because for small \Delta x,
    // the other terms of the amplification factor will be small in comparison
    // Then solve for \Delta t
    const real constants =
        std::sqrt(std::sqrt(6.0 * reynolds * prandtl / 64.0));
    const real ds = std::max(mesh.dx(), mesh.dy());
    const real dt = sigma_ratio * constants * (ds * ds);

    this->_space_assembly.flux_assembly(mesh, mesh, *_partial_mesh_1,
                                        this->time(), dt / 4.0);

    // Second stage
    // compute w(2) based on w(1) and the current timestep
    this->_space_assembly.flux_assembly(mesh, *_partial_mesh_1,
                                        *_partial_mesh_2,
                                        this->time() + dt / 2.0, dt / 3.0);

    // Third stage
    this->_space_assembly.flux_assembly(mesh, *_partial_mesh_2,
                                        *_partial_mesh_1,
                                        this->time() + dt / 2.0, dt / 2.0);

    // Fourth stage
    this->_space_assembly.flux_assembly(
        mesh, *_partial_mesh_1, *_partial_mesh_2, this->time() + dt / 2.0, dt);

    std::swap(this->_cur_mesh, _partial_mesh_2);
    this->_time += dt;
  }

 protected:
  std::unique_ptr<MeshT> _partial_mesh_1;
  std::unique_ptr<MeshT> _partial_mesh_2;
};

#endif  // _TIME_DISC_HPP_
