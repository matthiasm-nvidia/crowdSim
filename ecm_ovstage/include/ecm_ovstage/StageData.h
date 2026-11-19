#pragma once
// Small helpers around the ovstage C API, used by the adapter and available to host
// applications. Failures throw std::runtime_error carrying the SDK's error text.
#include <map>
#include <ovstage/ovstage.h>
#include <string>
#include <vector>

namespace ecm_ovstage::stage
{
ovx_string_t str(const char *text);
// Throws unless `status` is OVSTAGE_OK.
void check(ovstage_api_status_t status);
// Waits for an enqueued stage operation and releases it.
void wait(ovstage_instance_t *stage, ovstage_enqueue_result_t op);
// Waits for an enqueued USD population operation (open, add or remove a reference, apply changes).
void populationWait(ovstage_instance_t *stage, ovstage_population_enqueue_result_t op);
// Advances the stage's write floor to `ordinal` once everything for it has been written.
void seal(ovstage_instance_t *stage, ovstage_ordinal_t ordinal);
// Computes the hierarchy (world matrices, inherited state) at `ordinal`.
void hierarchy(ovstage_instance_t *stage, ovstage_ordinal_t ordinal);
std::string pathString(ovstage_instance_t *stage, ovx_primpath_t path);
ovx_primpath_t pathHandle(ovstage_instance_t *stage, const std::string &path);
// Native instancing: prototype roots, and the instance roots that place one prototype.
std::vector<std::string> prototypeRoots(ovstage_instance_t *stage);
std::vector<std::string> instanceRoots(ovstage_instance_t *stage, const std::string &prototypeRoot);

// One attribute value: numbers (lanes and array elements flattened) or a token.
struct Value
{
    std::vector<double> numbers;
    std::string token;
};
using Prim = std::map<std::string, Value>;    // attribute name -> value
using Snapshot = std::map<std::string, Prim>; // prim path -> attributes
// The named attributes, as of `ordinal`, of every prim that has at least one of them.
Snapshot read(ovstage_instance_t *stage, ovstage_ordinal_t ordinal, const std::vector<std::string> &names);

// A USD layer, given as text, referenced onto the stage's pseudo-root. An object belongs to
// one stage and must not outlive it.
class ReferencedLayer
{
  public:
    ReferencedLayer() = default;
    ReferencedLayer(const ReferencedLayer &) = delete;
    ReferencedLayer &operator=(const ReferencedLayer &) = delete;
    // Replaces the current layer with `layer` (empty removes it) and applies the change as
    // `ordinal`. The caller then seals the ordinal, computes the hierarchy and updates its renderer.
    void replace(ovstage_instance_t *stage, ovstage_ordinal_t ordinal, const std::string &layer);

  private:
    ovstage_population_usd_reference_handle_t handle = 0;
};
} // namespace ecm_ovstage::stage
