#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

OUTPUT_DIR="${OUTPUT_DIR:-acid_results}"
mkdir -p "${OUTPUT_DIR}"
export OUTPUT_DIR

SUMMARY_CSV="${SUMMARY_CSV:-${OUTPUT_DIR}/acid_test_summary.csv}"
echo "suite,status,results_csv" > "${SUMMARY_CSV}"

OVERALL_EXIT=0

FLAGS=""
if [[ "${1:-}" == "--local" ]]; then
    FLAGS="--local"
    shift
fi

# ── Local mode: one shared build before running any suite ───────────────────
if [[ "${FLAGS}" == "--local" ]]; then
    NPROC=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)
    echo "========================================"
    echo "Pre-building all test targets"
    echo "========================================"
    cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" \
        -DBUILD_SLOG_TESTS=ON \
        -DLOCK_MANAGER=DDR \
        -DREMASTER_PROTOCOL=COUNTERLESS \
        -DCMAKE_BUILD_TYPE=release || { echo "CMake configure failed."; exit 1; }
    make -C "${REPO_ROOT}/build" -j"${NPROC}" \
        consistency_singleregion_test \
        isolation_level_singleregion_test \
        consistency_multiregion_test \
        isolation_level_multiregion_test || { echo "Build failed."; exit 1; }
    export PREBUILT=1
fi

run_suite() {
    local name="$1"
    local script_path="$2"
    local results_csv="$3"

    echo "========================================"
    echo "Running ${name}"
    echo "========================================"

    bash "${script_path}" ${FLAGS}
    local exit_code=$?

    if [ ${exit_code} -eq 0 ]; then
        echo "${name},passed,${results_csv}" >> "${SUMMARY_CSV}"
    elif [ ${exit_code} -eq 2 ]; then
        echo "${name},skipped,${results_csv}" >> "${SUMMARY_CSV}"
    elif [ ${exit_code} -eq 3 ]; then
        echo "${name},not_implemented,${results_csv}" >> "${SUMMARY_CSV}"
    else
        OVERALL_EXIT=1
        echo "${name},failed,${results_csv}" >> "${SUMMARY_CSV}"
    fi

    echo ""
}

run_suite "singleregion_consistency" "${SCRIPT_DIR}/test_consistency_singleregion.sh" "${OUTPUT_DIR}/singleregion_consistency_results.csv"
run_suite "singleregion_isolation"   "${SCRIPT_DIR}/test_isolation_singleregion.sh"   "${OUTPUT_DIR}/singleregion_isolation_results.csv"
run_suite "multiregion_consistency"  "${SCRIPT_DIR}/test_consistency_multiregion.sh"  "${OUTPUT_DIR}/multiregion_consistency_results.csv"
run_suite "multiregion_isolation"    "${SCRIPT_DIR}/test_isolation_multiregion.sh"    "${OUTPUT_DIR}/multiregion_isolation_results.csv"

echo "========================================"
echo "All output written to ${OUTPUT_DIR}/"
echo "Summary: ${SUMMARY_CSV}"
echo "========================================"

exit "${OVERALL_EXIT}"
