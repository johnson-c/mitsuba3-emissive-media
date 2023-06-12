#pragma once

#include <mitsuba/core/object.h>
#include <mitsuba/core/spectrum.h>
#include <mitsuba/core/traits.h>
#include <mitsuba/render/fwd.h>
#include <drjit/vcall.h>

NAMESPACE_BEGIN(mitsuba)

enum class MediumEventSamplingMode : uint32_t {
    Analogue = 0,
    Maximum,
    Mean
};
MI_DECLARE_ENUM_OPERATORS(MediumEventSamplingMode)

template <typename Float, typename Spectrum>
class MI_EXPORT_LIB Medium : public Object {
public:
    MI_IMPORT_TYPES(PhaseFunction, Sampler, Scene, Texture, Emitter, Volume, EmitterPtr);

    /// Intersects a ray with the medium's bounding box
    virtual std::tuple<Mask, Float, Float>
    intersect_aabb(const Ray3f &ray) const = 0;

    /// Returns the medium's majorant used for delta tracking
    virtual UnpolarizedSpectrum
    get_majorant(const MediumInteraction3f &mi,
                 Mask active = true) const = 0;

    /// Returns the medium coefficients Sigma_s, Sigma_n and Sigma_t evaluated
    /// at a given MediumInteraction mi
    virtual std::tuple<UnpolarizedSpectrum, UnpolarizedSpectrum,
                       UnpolarizedSpectrum>
    get_scattering_coefficients(const MediumInteraction3f &mi,
                                Mask active = true) const = 0;

    /// Returns the radiance, the probability of a scatter event, and
    /// the weights associated with real and null scattering events
    std::tuple<std::pair<UnpolarizedSpectrum, UnpolarizedSpectrum>,
               std::pair<UnpolarizedSpectrum, UnpolarizedSpectrum>>
    get_interaction_probabilities(const Spectrum &radiance,
                                  const MediumInteraction3f &mi,
                                  const Spectrum &throughput) const;

    MI_INLINE std::tuple<UnpolarizedSpectrum, UnpolarizedSpectrum>
    medium_probabilities_analog(const UnpolarizedSpectrum &radiance,
                                const MediumInteraction3f &mi) const {
        auto prob_s = mi.sigma_t;
        auto prob_n = mi.sigma_n + dr::maximum(radiance, dr::mean(dr::abs(radiance)));
        return std::tuple{ prob_s, prob_n };
    }

    MI_INLINE std::tuple<UnpolarizedSpectrum, UnpolarizedSpectrum>
    medium_probabilities_max(const UnpolarizedSpectrum &radiance,
                             const MediumInteraction3f &mi,
                             const UnpolarizedSpectrum &throughput) const {
        auto prob_s = dr::max(dr::abs(mi.sigma_t * throughput));
        auto prob_n = dr::max(dr::abs(mi.sigma_n * throughput)) +
                      dr::max(dr::abs(radiance * dr::maximum(1.f, throughput)));
        return std::tuple{ prob_s, prob_n };
    }

    MI_INLINE std::tuple<UnpolarizedSpectrum, UnpolarizedSpectrum>
    medium_probabilities_mean(const UnpolarizedSpectrum &radiance,
                              const MediumInteraction3f &mi,
                              const UnpolarizedSpectrum &throughput) const {
        auto prob_s = dr::mean(dr::abs(mi.sigma_t * throughput));
        auto prob_n = dr::mean(dr::abs(mi.sigma_n * throughput)) +
                      dr::mean(dr::abs(radiance * (0.5f + 0.5f * throughput)));
        return std::tuple{ prob_s, prob_n };
    }

    /// Returns the medium's radiance used for emissive media
    UnpolarizedSpectrum
    get_radiance(const MediumInteraction3f &mi,
                 Mask active = true) const;

    /**
     * \brief Sample a free-flight distance in the medium.
     *
     * This function samples a (tentative) free-flight distance according to an
     * exponential transmittance. It is then up to the integrator to then decide
     * whether the MediumInteraction corresponds to a real or null scattering
     * event.
     *
     * \param ray      Ray, along which a distance should be sampled
     * \param sample   A uniformly distributed random sample
     * \param channel  The channel according to which we will sample the
     * free-flight distance. This argument is only used when rendering in RGB
     * modes.
     *
     * \return         This method returns a MediumInteraction.
     *                 The MediumInteraction will always be valid,
     *                 except if the ray missed the Medium's bounding box.
     */
    MediumInteraction3f sample_interaction(const Ray3f &ray, Float sample,
                                           UInt32 channel, Mask active) const;

    /**
     * \brief Compute the transmittance and PDF
     *
     * This function evaluates the transmittance and PDF of sampling a certain
     * free-flight distance The returned PDF takes into account if a medium
     * interaction occurred (mi.t <= si.t) or the ray left the medium (mi.t >
     * si.t)
     *
     * The evaluated PDF is spectrally varying. This allows to account for the
     * fact that the free-flight distance sampling distribution can depend on
     * the wavelength.
     *
     * \return   This method returns a pair of (Transmittance, PDF).
     *
     */
    std::pair<UnpolarizedSpectrum, UnpolarizedSpectrum>
    transmittance_eval_pdf(const MediumInteraction3f &mi,
                           const SurfaceInteraction3f &si,
                           Mask active) const;

    /// Return the phase function of this medium
    MI_INLINE const PhaseFunction *phase_function() const {
        return m_phase_function.get();
    }

    /// Return the emitter of this medium
    const EmitterPtr emitter(Mask /*unused*/ = true) const {
        return m_emitter.get();
    }

    /// Return the emitter of this medium
    EmitterPtr emitter(Mask /*unused*/ = true) {
        return m_emitter.get();
    }

    /// Returns whether this specific medium instance uses emitter sampling
    MI_INLINE bool use_emitter_sampling() const { return m_sample_emitters; }

    /// Returns whether this medium is homogeneous
    MI_INLINE bool is_homogeneous() const { return m_is_homogeneous; }

    /// Returns whether this medium is emitting
    MI_INLINE bool is_emitter() const { return m_emitter.get() != nullptr; }

    /// Returns whether this medium has a spectrally varying extinction
    MI_INLINE bool has_spectral_extinction() const {
        return m_has_spectral_extinction;
    }

    /// Returns whether this medium is homogeneous
    void set_emitter(Emitter* emitter);

    void traverse(TraversalCallback *callback) override;

    /// Return a string identifier
    std::string id() const override { return m_id; }

    /// Set a string identifier
    void set_id(const std::string& id) override { m_id = id; };

    /// Return a human-readable representation of the Medium
    std::string to_string() const override = 0;

    DRJIT_VCALL_REGISTER(Float, mitsuba::Medium)

    MI_DECLARE_CLASS()
protected:
    Medium();
    Medium(const Properties &props);
    virtual ~Medium();

protected:
    ref<PhaseFunction> m_phase_function;
    ref<Emitter> m_emitter;
    bool m_sample_emitters, m_is_homogeneous, m_has_spectral_extinction;
    MediumEventSamplingMode m_medium_sampling_mode;

    /// Identifier (if available)
    std::string m_id;
};

MI_EXTERN_CLASS(Medium)
NAMESPACE_END(mitsuba)

// -----------------------------------------------------------------------
//! @{ \name Dr.Jit support for packets of Medium pointers
// -----------------------------------------------------------------------

DRJIT_VCALL_TEMPLATE_BEGIN(mitsuba::Medium)
    DRJIT_VCALL_GETTER(phase_function, const typename Class::PhaseFunction*)
    DRJIT_VCALL_GETTER(emitter, const typename Class::Emitter*)
    DRJIT_VCALL_GETTER(use_emitter_sampling, bool)
    DRJIT_VCALL_GETTER(is_homogeneous, bool)
    DRJIT_VCALL_GETTER(has_spectral_extinction, bool)
    DRJIT_VCALL_METHOD(get_majorant)
    DRJIT_VCALL_METHOD(get_radiance)
    DRJIT_VCALL_METHOD(intersect_aabb)
    DRJIT_VCALL_METHOD(sample_interaction)
    DRJIT_VCALL_METHOD(transmittance_eval_pdf)
    DRJIT_VCALL_METHOD(get_scattering_coefficients)
    DRJIT_VCALL_METHOD(get_interaction_probabilities)
    DRJIT_VCALL_METHOD(medium_probabilities_analog)
    DRJIT_VCALL_METHOD(medium_probabilities_max)
    DRJIT_VCALL_METHOD(medium_probabilities_mean)
    auto is_emitter() const { return neq(emitter(), nullptr); }
DRJIT_VCALL_TEMPLATE_END(mitsuba::Medium)

//! @}
// -----------------------------------------------------------------------
