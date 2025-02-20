/*************************************************************************/
/*  godot_body_3d.cpp                                                    */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2021 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2021 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#include "godot_body_3d.h"

#include "godot_area_3d.h"
#include "godot_body_direct_state_3d.h"
#include "godot_space_3d.h"

void GodotBody3D::_mass_properties_changed() {
	if (get_space() && !mass_properties_update_list.in_list() && (calculate_inertia || calculate_center_of_mass)) {
		get_space()->body_add_to_mass_properties_update_list(&mass_properties_update_list);
	}
}

void GodotBody3D::_update_transform_dependent() {
	center_of_mass = get_transform().basis.xform(center_of_mass_local);
	principal_inertia_axes = get_transform().basis * principal_inertia_axes_local;

	// Update inertia tensor.
	Basis tb = principal_inertia_axes;
	Basis tbt = tb.transposed();
	Basis diag;
	diag.scale(_inv_inertia);
	_inv_inertia_tensor = tb * diag * tbt;
}

void GodotBody3D::update_mass_properties() {
	// Update shapes and motions.

	switch (mode) {
		case PhysicsServer3D::BODY_MODE_DYNAMIC: {
			real_t total_area = 0;
			for (int i = 0; i < get_shape_count(); i++) {
				if (is_shape_disabled(i)) {
					continue;
				}

				total_area += get_shape_area(i);
			}

			if (calculate_center_of_mass) {
				// We have to recompute the center of mass.
				center_of_mass_local.zero();

				if (total_area != 0.0) {
					for (int i = 0; i < get_shape_count(); i++) {
						if (is_shape_disabled(i)) {
							continue;
						}

						real_t area = get_shape_area(i);

						real_t mass = area * this->mass / total_area;

						// NOTE: we assume that the shape origin is also its center of mass.
						center_of_mass_local += mass * get_shape_transform(i).origin;
					}

					center_of_mass_local /= mass;
				}
			}

			if (calculate_inertia) {
				// Recompute the inertia tensor.
				Basis inertia_tensor;
				inertia_tensor.set_zero();
				bool inertia_set = false;

				for (int i = 0; i < get_shape_count(); i++) {
					if (is_shape_disabled(i)) {
						continue;
					}

					real_t area = get_shape_area(i);
					if (area == 0.0) {
						continue;
					}

					inertia_set = true;

					const GodotShape3D *shape = get_shape(i);

					real_t mass = area * this->mass / total_area;

					Basis shape_inertia_tensor = Basis::from_scale(shape->get_moment_of_inertia(mass));
					Transform3D shape_transform = get_shape_transform(i);
					Basis shape_basis = shape_transform.basis.orthonormalized();

					// NOTE: we don't take the scale of collision shapes into account when computing the inertia tensor!
					shape_inertia_tensor = shape_basis * shape_inertia_tensor * shape_basis.transposed();

					Vector3 shape_origin = shape_transform.origin - center_of_mass_local;
					inertia_tensor += shape_inertia_tensor + (Basis() * shape_origin.dot(shape_origin) - shape_origin.outer(shape_origin)) * mass;
				}

				// Set the inertia to a valid value when there are no valid shapes.
				if (!inertia_set) {
					inertia_tensor = Basis();
				}

				// Handle partial custom inertia.
				if (inertia.x > 0.0) {
					inertia_tensor[0][0] = inertia.x;
				}
				if (inertia.y > 0.0) {
					inertia_tensor[1][1] = inertia.y;
				}
				if (inertia.z > 0.0) {
					inertia_tensor[2][2] = inertia.z;
				}

				// Compute the principal axes of inertia.
				principal_inertia_axes_local = inertia_tensor.diagonalize().transposed();
				_inv_inertia = inertia_tensor.get_main_diagonal().inverse();
			}

			if (mass) {
				_inv_mass = 1.0 / mass;
			} else {
				_inv_mass = 0;
			}

		} break;
		case PhysicsServer3D::BODY_MODE_KINEMATIC:
		case PhysicsServer3D::BODY_MODE_STATIC: {
			_inv_inertia = Vector3();
			_inv_mass = 0;
		} break;
		case PhysicsServer3D::BODY_MODE_DYNAMIC_LINEAR: {
			_inv_inertia_tensor.set_zero();
			_inv_mass = 1.0 / mass;

		} break;
	}

	_update_transform_dependent();
}

void GodotBody3D::reset_mass_properties() {
	calculate_inertia = true;
	calculate_center_of_mass = true;
	_mass_properties_changed();
}

void GodotBody3D::set_active(bool p_active) {
	if (active == p_active) {
		return;
	}

	active = p_active;

	if (active) {
		if (mode == PhysicsServer3D::BODY_MODE_STATIC) {
			// Static bodies can't be active.
			active = false;
		} else if (get_space()) {
			get_space()->body_add_to_active_list(&active_list);
		}
	} else if (get_space()) {
		get_space()->body_remove_from_active_list(&active_list);
	}
}

void GodotBody3D::set_param(PhysicsServer3D::BodyParameter p_param, const Variant &p_value) {
	switch (p_param) {
		case PhysicsServer3D::BODY_PARAM_BOUNCE: {
			bounce = p_value;
		} break;
		case PhysicsServer3D::BODY_PARAM_FRICTION: {
			friction = p_value;
		} break;
		case PhysicsServer3D::BODY_PARAM_MASS: {
			real_t mass_value = p_value;
			ERR_FAIL_COND(mass_value <= 0);
			mass = mass_value;
			if (mode >= PhysicsServer3D::BODY_MODE_DYNAMIC) {
				_mass_properties_changed();
			}
		} break;
		case PhysicsServer3D::BODY_PARAM_INERTIA: {
			inertia = p_value;
			if ((inertia.x <= 0.0) || (inertia.y <= 0.0) || (inertia.z <= 0.0)) {
				calculate_inertia = true;
				if (mode == PhysicsServer3D::BODY_MODE_DYNAMIC) {
					_mass_properties_changed();
				}
			} else {
				calculate_inertia = false;
				if (mode == PhysicsServer3D::BODY_MODE_DYNAMIC) {
					principal_inertia_axes_local = Basis();
					_inv_inertia = inertia.inverse();
					_update_transform_dependent();
				}
			}
		} break;
		case PhysicsServer3D::BODY_PARAM_CENTER_OF_MASS: {
			calculate_center_of_mass = false;
			center_of_mass_local = p_value;
			_update_transform_dependent();
		} break;
		case PhysicsServer3D::BODY_PARAM_GRAVITY_SCALE: {
			gravity_scale = p_value;
		} break;
		case PhysicsServer3D::BODY_PARAM_LINEAR_DAMP: {
			linear_damp = p_value;
		} break;
		case PhysicsServer3D::BODY_PARAM_ANGULAR_DAMP: {
			angular_damp = p_value;
		} break;
		default: {
		}
	}
}

Variant GodotBody3D::get_param(PhysicsServer3D::BodyParameter p_param) const {
	switch (p_param) {
		case PhysicsServer3D::BODY_PARAM_BOUNCE: {
			return bounce;
		} break;
		case PhysicsServer3D::BODY_PARAM_FRICTION: {
			return friction;
		} break;
		case PhysicsServer3D::BODY_PARAM_MASS: {
			return mass;
		} break;
		case PhysicsServer3D::BODY_PARAM_INERTIA: {
			if (mode == PhysicsServer3D::BODY_MODE_DYNAMIC) {
				return _inv_inertia.inverse();
			} else {
				return Vector3();
			}
		} break;
		case PhysicsServer3D::BODY_PARAM_CENTER_OF_MASS: {
			return center_of_mass;
		} break;
		case PhysicsServer3D::BODY_PARAM_GRAVITY_SCALE: {
			return gravity_scale;
		} break;
		case PhysicsServer3D::BODY_PARAM_LINEAR_DAMP: {
			return linear_damp;
		} break;
		case PhysicsServer3D::BODY_PARAM_ANGULAR_DAMP: {
			return angular_damp;
		} break;

		default: {
		}
	}

	return 0;
}

void GodotBody3D::set_mode(PhysicsServer3D::BodyMode p_mode) {
	PhysicsServer3D::BodyMode prev = mode;
	mode = p_mode;

	switch (p_mode) {
		case PhysicsServer3D::BODY_MODE_STATIC:
		case PhysicsServer3D::BODY_MODE_KINEMATIC: {
			_set_inv_transform(get_transform().affine_inverse());
			_inv_mass = 0;
			_inv_inertia = Vector3();
			_set_static(p_mode == PhysicsServer3D::BODY_MODE_STATIC);
			set_active(p_mode == PhysicsServer3D::BODY_MODE_KINEMATIC && contacts.size());
			linear_velocity = Vector3();
			angular_velocity = Vector3();
			if (mode == PhysicsServer3D::BODY_MODE_KINEMATIC && prev != mode) {
				first_time_kinematic = true;
			}
			_update_transform_dependent();

		} break;
		case PhysicsServer3D::BODY_MODE_DYNAMIC: {
			_inv_mass = mass > 0 ? (1.0 / mass) : 0;
			if (!calculate_inertia) {
				principal_inertia_axes_local = Basis();
				_inv_inertia = inertia.inverse();
				_update_transform_dependent();
			}
			_mass_properties_changed();
			_set_static(false);
			set_active(true);

		} break;
		case PhysicsServer3D::BODY_MODE_DYNAMIC_LINEAR: {
			_inv_mass = mass > 0 ? (1.0 / mass) : 0;
			_inv_inertia = Vector3();
			angular_velocity = Vector3();
			_update_transform_dependent();
			_set_static(false);
			set_active(true);
		}
	}
}

PhysicsServer3D::BodyMode GodotBody3D::get_mode() const {
	return mode;
}

void GodotBody3D::_shapes_changed() {
	_mass_properties_changed();
}

void GodotBody3D::set_state(PhysicsServer3D::BodyState p_state, const Variant &p_variant) {
	switch (p_state) {
		case PhysicsServer3D::BODY_STATE_TRANSFORM: {
			if (mode == PhysicsServer3D::BODY_MODE_KINEMATIC) {
				new_transform = p_variant;
				//wakeup_neighbours();
				set_active(true);
				if (first_time_kinematic) {
					_set_transform(p_variant);
					_set_inv_transform(get_transform().affine_inverse());
					first_time_kinematic = false;
				}

			} else if (mode == PhysicsServer3D::BODY_MODE_STATIC) {
				_set_transform(p_variant);
				_set_inv_transform(get_transform().affine_inverse());
				wakeup_neighbours();
			} else {
				Transform3D t = p_variant;
				t.orthonormalize();
				new_transform = get_transform(); //used as old to compute motion
				if (new_transform == t) {
					break;
				}
				_set_transform(t);
				_set_inv_transform(get_transform().inverse());
				_update_transform_dependent();
			}
			wakeup();

		} break;
		case PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY: {
			linear_velocity = p_variant;
			constant_linear_velocity = linear_velocity;
			wakeup();
		} break;
		case PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY: {
			angular_velocity = p_variant;
			constant_angular_velocity = angular_velocity;
			wakeup();

		} break;
		case PhysicsServer3D::BODY_STATE_SLEEPING: {
			if (mode == PhysicsServer3D::BODY_MODE_STATIC || mode == PhysicsServer3D::BODY_MODE_KINEMATIC) {
				break;
			}
			bool do_sleep = p_variant;
			if (do_sleep) {
				linear_velocity = Vector3();
				//biased_linear_velocity=Vector3();
				angular_velocity = Vector3();
				//biased_angular_velocity=Vector3();
				set_active(false);
			} else {
				set_active(true);
			}
		} break;
		case PhysicsServer3D::BODY_STATE_CAN_SLEEP: {
			can_sleep = p_variant;
			if (mode >= PhysicsServer3D::BODY_MODE_DYNAMIC && !active && !can_sleep) {
				set_active(true);
			}

		} break;
	}
}

Variant GodotBody3D::get_state(PhysicsServer3D::BodyState p_state) const {
	switch (p_state) {
		case PhysicsServer3D::BODY_STATE_TRANSFORM: {
			return get_transform();
		} break;
		case PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY: {
			return linear_velocity;
		} break;
		case PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY: {
			return angular_velocity;
		} break;
		case PhysicsServer3D::BODY_STATE_SLEEPING: {
			return !is_active();
		} break;
		case PhysicsServer3D::BODY_STATE_CAN_SLEEP: {
			return can_sleep;
		} break;
	}

	return Variant();
}

void GodotBody3D::set_space(GodotSpace3D *p_space) {
	if (get_space()) {
		if (mass_properties_update_list.in_list()) {
			get_space()->body_remove_from_mass_properties_update_list(&mass_properties_update_list);
		}
		if (active_list.in_list()) {
			get_space()->body_remove_from_active_list(&active_list);
		}
		if (direct_state_query_list.in_list()) {
			get_space()->body_remove_from_state_query_list(&direct_state_query_list);
		}
	}

	_set_space(p_space);

	if (get_space()) {
		_mass_properties_changed();
		if (active) {
			get_space()->body_add_to_active_list(&active_list);
		}
	}
}

void GodotBody3D::_compute_area_gravity_and_damping(const GodotArea3D *p_area) {
	Vector3 area_gravity;
	p_area->compute_gravity(get_transform().get_origin(), area_gravity);
	gravity += area_gravity;

	area_linear_damp += p_area->get_linear_damp();
	area_angular_damp += p_area->get_angular_damp();
}

void GodotBody3D::set_axis_lock(PhysicsServer3D::BodyAxis p_axis, bool lock) {
	if (lock) {
		locked_axis |= p_axis;
	} else {
		locked_axis &= ~p_axis;
	}
}

bool GodotBody3D::is_axis_locked(PhysicsServer3D::BodyAxis p_axis) const {
	return locked_axis & p_axis;
}

void GodotBody3D::integrate_forces(real_t p_step) {
	if (mode == PhysicsServer3D::BODY_MODE_STATIC) {
		return;
	}

	GodotArea3D *def_area = get_space()->get_default_area();

	ERR_FAIL_COND(!def_area);

	int ac = areas.size();
	bool stopped = false;
	gravity = Vector3(0, 0, 0);
	area_linear_damp = 0;
	area_angular_damp = 0;
	if (ac) {
		areas.sort();
		const AreaCMP *aa = &areas[0];
		// damp_area = aa[ac-1].area;
		for (int i = ac - 1; i >= 0 && !stopped; i--) {
			PhysicsServer3D::AreaSpaceOverrideMode mode = aa[i].area->get_space_override_mode();
			switch (mode) {
				case PhysicsServer3D::AREA_SPACE_OVERRIDE_COMBINE:
				case PhysicsServer3D::AREA_SPACE_OVERRIDE_COMBINE_REPLACE: {
					_compute_area_gravity_and_damping(aa[i].area);
					stopped = mode == PhysicsServer3D::AREA_SPACE_OVERRIDE_COMBINE_REPLACE;
				} break;
				case PhysicsServer3D::AREA_SPACE_OVERRIDE_REPLACE:
				case PhysicsServer3D::AREA_SPACE_OVERRIDE_REPLACE_COMBINE: {
					gravity = Vector3(0, 0, 0);
					area_angular_damp = 0;
					area_linear_damp = 0;
					_compute_area_gravity_and_damping(aa[i].area);
					stopped = mode == PhysicsServer3D::AREA_SPACE_OVERRIDE_REPLACE;
				} break;
				default: {
				}
			}
		}
	}

	if (!stopped) {
		_compute_area_gravity_and_damping(def_area);
	}

	gravity *= gravity_scale;

	// If less than 0, override dampenings with that of the Body
	if (angular_damp >= 0) {
		area_angular_damp = angular_damp;
	}
	/*
	else
		area_angular_damp=damp_area->get_angular_damp();
	*/

	if (linear_damp >= 0) {
		area_linear_damp = linear_damp;
	}
	/*
	else
		area_linear_damp=damp_area->get_linear_damp();
	*/

	Vector3 motion;
	bool do_motion = false;

	if (mode == PhysicsServer3D::BODY_MODE_KINEMATIC) {
		//compute motion, angular and etc. velocities from prev transform
		motion = new_transform.origin - get_transform().origin;
		do_motion = true;
		linear_velocity = constant_linear_velocity + motion / p_step;

		//compute a FAKE angular velocity, not so easy
		Basis rot = new_transform.basis.orthonormalized() * get_transform().basis.orthonormalized().transposed();
		Vector3 axis;
		real_t angle;

		rot.get_axis_angle(axis, angle);
		axis.normalize();
		angular_velocity = constant_angular_velocity + axis * (angle / p_step);
	} else {
		if (!omit_force_integration) {
			//overridden by direct state query

			Vector3 force = gravity * mass;
			force += applied_force;
			Vector3 torque = applied_torque;

			real_t damp = 1.0 - p_step * area_linear_damp;

			if (damp < 0) { // reached zero in the given time
				damp = 0;
			}

			real_t angular_damp = 1.0 - p_step * area_angular_damp;

			if (angular_damp < 0) { // reached zero in the given time
				angular_damp = 0;
			}

			linear_velocity *= damp;
			angular_velocity *= angular_damp;

			linear_velocity += _inv_mass * force * p_step;
			angular_velocity += _inv_inertia_tensor.xform(torque) * p_step;
		}

		if (continuous_cd) {
			motion = linear_velocity * p_step;
			do_motion = true;
		}
	}

	applied_force = Vector3();
	applied_torque = Vector3();

	//motion=linear_velocity*p_step;

	biased_angular_velocity = Vector3();
	biased_linear_velocity = Vector3();

	if (do_motion) { //shapes temporarily extend for raycast
		_update_shapes_with_motion(motion);
	}

	def_area = nullptr; // clear the area, so it is set in the next frame
	contact_count = 0;
}

void GodotBody3D::integrate_velocities(real_t p_step) {
	if (mode == PhysicsServer3D::BODY_MODE_STATIC) {
		return;
	}

	if (fi_callback_data || body_state_callback) {
		get_space()->body_add_to_state_query_list(&direct_state_query_list);
	}

	//apply axis lock linear
	for (int i = 0; i < 3; i++) {
		if (is_axis_locked((PhysicsServer3D::BodyAxis)(1 << i))) {
			linear_velocity[i] = 0;
			biased_linear_velocity[i] = 0;
			new_transform.origin[i] = get_transform().origin[i];
		}
	}
	//apply axis lock angular
	for (int i = 0; i < 3; i++) {
		if (is_axis_locked((PhysicsServer3D::BodyAxis)(1 << (i + 3)))) {
			angular_velocity[i] = 0;
			biased_angular_velocity[i] = 0;
		}
	}

	if (mode == PhysicsServer3D::BODY_MODE_KINEMATIC) {
		_set_transform(new_transform, false);
		_set_inv_transform(new_transform.affine_inverse());
		if (contacts.size() == 0 && linear_velocity == Vector3() && angular_velocity == Vector3()) {
			set_active(false); //stopped moving, deactivate
		}

		return;
	}

	Vector3 total_angular_velocity = angular_velocity + biased_angular_velocity;

	real_t ang_vel = total_angular_velocity.length();
	Transform3D transform = get_transform();

	if (!Math::is_zero_approx(ang_vel)) {
		Vector3 ang_vel_axis = total_angular_velocity / ang_vel;
		Basis rot(ang_vel_axis, ang_vel * p_step);
		Basis identity3(1, 0, 0, 0, 1, 0, 0, 0, 1);
		transform.origin += ((identity3 - rot) * transform.basis).xform(center_of_mass_local);
		transform.basis = rot * transform.basis;
		transform.orthonormalize();
	}

	Vector3 total_linear_velocity = linear_velocity + biased_linear_velocity;
	/*for(int i=0;i<3;i++) {
		if (axis_lock&(1<<i)) {
			transform.origin[i]=0.0;
		}
	}*/

	transform.origin += total_linear_velocity * p_step;

	_set_transform(transform);
	_set_inv_transform(get_transform().inverse());

	_update_transform_dependent();
}

void GodotBody3D::wakeup_neighbours() {
	for (const KeyValue<GodotConstraint3D *, int> &E : constraint_map) {
		const GodotConstraint3D *c = E.key;
		GodotBody3D **n = c->get_body_ptr();
		int bc = c->get_body_count();

		for (int i = 0; i < bc; i++) {
			if (i == E.value) {
				continue;
			}
			GodotBody3D *b = n[i];
			if (b->mode < PhysicsServer3D::BODY_MODE_DYNAMIC) {
				continue;
			}

			if (!b->is_active()) {
				b->set_active(true);
			}
		}
	}
}

void GodotBody3D::call_queries() {
	if (fi_callback_data) {
		if (!fi_callback_data->callable.get_object()) {
			set_force_integration_callback(Callable());
		} else {
			Variant direct_state_variant = get_direct_state();
			const Variant *vp[2] = { &direct_state_variant, &fi_callback_data->udata };

			Callable::CallError ce;
			int argc = (fi_callback_data->udata.get_type() == Variant::NIL) ? 1 : 2;
			Variant rv;
			fi_callback_data->callable.call(vp, argc, rv, ce);
		}
	}

	if (body_state_callback_instance) {
		(body_state_callback)(body_state_callback_instance, get_direct_state());
	}
}

bool GodotBody3D::sleep_test(real_t p_step) {
	if (mode == PhysicsServer3D::BODY_MODE_STATIC || mode == PhysicsServer3D::BODY_MODE_KINEMATIC) {
		return true;
	} else if (!can_sleep) {
		return false;
	}

	if (Math::abs(angular_velocity.length()) < get_space()->get_body_angular_velocity_sleep_threshold() && Math::abs(linear_velocity.length_squared()) < get_space()->get_body_linear_velocity_sleep_threshold() * get_space()->get_body_linear_velocity_sleep_threshold()) {
		still_time += p_step;

		return still_time > get_space()->get_body_time_to_sleep();
	} else {
		still_time = 0; //maybe this should be set to 0 on set_active?
		return false;
	}
}

void GodotBody3D::set_state_sync_callback(void *p_instance, PhysicsServer3D::BodyStateCallback p_callback) {
	body_state_callback_instance = p_instance;
	body_state_callback = p_callback;
}

void GodotBody3D::set_force_integration_callback(const Callable &p_callable, const Variant &p_udata) {
	if (p_callable.get_object()) {
		if (!fi_callback_data) {
			fi_callback_data = memnew(ForceIntegrationCallbackData);
		}
		fi_callback_data->callable = p_callable;
		fi_callback_data->udata = p_udata;
	} else if (fi_callback_data) {
		memdelete(fi_callback_data);
		fi_callback_data = nullptr;
	}
}

GodotPhysicsDirectBodyState3D *GodotBody3D::get_direct_state() {
	if (!direct_state) {
		direct_state = memnew(GodotPhysicsDirectBodyState3D);
		direct_state->body = this;
	}
	return direct_state;
}

GodotBody3D::GodotBody3D() :
		GodotCollisionObject3D(TYPE_BODY),
		active_list(this),
		mass_properties_update_list(this),
		direct_state_query_list(this) {
	_set_static(false);
}

GodotBody3D::~GodotBody3D() {
	if (fi_callback_data) {
		memdelete(fi_callback_data);
	}
	if (direct_state) {
		memdelete(direct_state);
	}
}
