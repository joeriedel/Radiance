/*! \file Light.cpp
	\copyright Copyright (c) 2013 Sunside Inc., All Rights Reserved.
	\copyright See Radiance/LICENSE for licensing terms.
	\author Joe Riedel
	\ingroup world
*/

#include RADPCH
#include "World.h"
#include "Light.h"
#include "Entity.h"

namespace world {

Light::Light(World *w) : 
m_interactionHead(0),
m_pos(Vec3::Zero),
m_style(kStyle_Diffuse|kStyle_Specular|kStyle_CastShadows),
m_intensityTime(0.0),
m_dfTime(0.0),
m_spTime(0.0),
m_radius(400.f),
m_interactionFlags(0),
m_intensityStep(0),
m_dfStep(0),
m_spStep(0),
m_intensityLoop(false),
m_dfLoop(false),
m_spLoop(false),
m_leaf(0),
m_prev(0),
m_next(0),
m_world(w),
m_markFrame(-1),
m_visFrame(-1),
m_drawFrame(-1) {
	m_intensity[0] = m_intensity[1] = 1.f;
	m_shColor = Vec4(0,0,0,1);
	m_spColor[0] = m_spColor[1] = Vec3(1,1,1);
	m_dfColor[0] = m_dfColor[1] = Vec3(1,1,1);
	m_bounds = BBox(-Vec3(m_radius, m_radius, m_radius), Vec3(m_radius, m_radius, m_radius));
}

Light::~Light() {
}

void Light::Link() {
	BBox bounds(m_bounds);
	bounds.Translate(m_pos);

	m_world->LinkLight(
		*this,
		bounds
	);
}

void Light::Unlink() {
	m_world->UnlinkLight(*this);
}

void Light::AnimateIntensity(float dt) {
	if (!m_intensitySteps.empty()) {
		double baseTime = 0.0;
		m_intensityTime += (double)dt;

		const IntensityStep *cur = &m_intensitySteps[m_intensityStep];

		while (m_intensityTime > cur->time) {

			m_intensity[1] = cur->intensity;
			
			++m_intensityStep;
			
			if (m_intensityStep >= (int)m_intensitySteps.size()) {
				if (m_intensityLoop) {
					m_intensityStep = 0;
					m_intensityTime -= cur->time;
					cur = &m_intensitySteps[0];
				} else {
					m_intensity[0] = m_intensity[1] = cur->intensity;
					m_intensitySteps.clear();
					return;
				}
			}
			
			cur = &m_intensitySteps[m_intensityStep];
		}

		if (m_intensityStep > 0) {
			baseTime = m_intensitySteps[m_intensityStep-1].time;
		}

		float dt = cur->time - baseTime;
		float offset = m_intensityTime - baseTime;
		m_intensity[0] = math::Lerp(m_intensity[1], cur->intensity, offset / dt);
	}
}

void Light::AnimateColor(
	float dt,
	ColorStep::Vec &steps,
	int &index,
	double &time,
	Vec3 *color,
	bool loop
) {
	if (!steps.empty()) {
		double baseTime = 0.0;
		time += (double)dt;

		const ColorStep *cur = &steps[index];

		while (time > cur->time) {

			color[1] = cur->color;
			
			++index;
			
			if (index >= (int)steps.size()) {
				if (loop) {
					index = 0;
					time -= cur->time;
					cur = &steps[0];
				} else {
					color[0] = color[1] = cur->color;
					steps.clear();
					return;
				}
			}
			
			cur = &steps[index];
		}

		if (index > 0) {
			baseTime = steps[index-1].time;
		}

		float dt = cur->time - baseTime;
		float offset = time - baseTime;
		color[0] = math::Lerp(color[1], cur->color, offset / dt);
	}
}

void Light::Tick(float dt) {

	AnimateIntensity(dt);
	
	AnimateColor(
		dt,
		m_dfSteps,
		m_dfStep,
		m_dfTime,
		m_dfColor,
		m_dfLoop
	);

	AnimateColor(
		dt,
		m_spSteps,
		m_spStep,
		m_spTime,
		m_spColor,
		m_spLoop
	);

}

void Light::AnimateIntensity(const IntensityStep::Vec &vec, bool loop) {
	if (vec.empty()) {
		m_intensitySteps.clear();
		return;
	}
	if (vec.size() < 2) {
		m_intensity[0] = vec[0].intensity;
		m_intensitySteps.clear();
		return;
	}

	m_intensityLoop = loop;
	m_intensityTime = 0.0;
	m_intensityStep = 0;
	m_intensity[1] = m_intensity[0];
	m_intensitySteps = vec;

	double dt = 0.0;
	for (size_t i = 0; i < m_intensitySteps.size(); ++i) {
		m_intensitySteps[i].time += dt;
		dt += m_intensitySteps[i].time;
	}
}

void Light::InitColorSteps(
	const ColorStep::Vec &srcVec,
	bool srcLoop,
	ColorStep::Vec &vec,
	Vec3 *color,
	double &time,
	int &index,
	bool &loop
) {
	if (srcVec.empty()) {
		vec.clear();
		return;
	}
	if (srcVec.size() < 2) {
		color[0] = color[1] = srcVec[0].color;
		vec.clear();
		return;
	}

	loop = srcLoop;
	time = 0.0;
	index = 0;

	vec = srcVec;
	color[1] = color[0];

	double dt = 0.0;
	for (size_t i = 0; i < vec.size(); ++i) {
		vec[i].time += dt;
		dt += vec[i].time;
	}
}

void Light::AnimateDiffuseColor(const ColorStep::Vec &vec, bool loop) {
	InitColorSteps(
		vec,
		loop,
		m_dfSteps,
		m_dfColor,
		m_dfTime,
		m_dfStep,
		m_dfLoop
	);
}

void Light::AnimateSpecularColor(const ColorStep::Vec &vec, bool loop) {
	InitColorSteps(
		vec,
		loop,
		m_spSteps,
		m_spColor,
		m_spTime,
		m_spStep,
		m_spLoop
	);
}

details::LightInteraction **Light::ChainHead(int matId) {
	std::pair<details::MatInteractionChain::iterator, bool> r = 
		m_matInteractionChain.insert(details::MatInteractionChain::value_type(matId, (details::LightInteraction *)0));
	return &r.first->second;
}

} // world
