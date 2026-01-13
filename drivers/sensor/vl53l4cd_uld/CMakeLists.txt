# SPDX-License-Identifier: Apache-2.0
zephyr_library()

# ---- Point to your ULD location under modules/hal/st ----
# ZEPHYR_BASE is .../zephyrproject/zephyr
# So the ULD dir is one level up, in modules/hal/st/sensor/vl53l4cd_uld
set(VL53L4CD_CORE_SRC ${ZEPHYR_BASE}/../modules/hal/st/sensor/vl53l4cd_uld/api/core/src)
set(VL53L4CD_CORE_INC ${ZEPHYR_BASE}/../modules/hal/st/sensor/vl53l4cd_uld/api/core/inc)

# Make sure the driver dir (this one) and the external ULD dir are on the include path,
# so #include "VL53L4CD_api.h" and "platform.h" both resolve.
zephyr_include_directories(
  ${CMAKE_CURRENT_LIST_DIR}
  ${VL53L4CD_CORE_INC}
)

# Build the driver + your platform + the vendor ULD sources
zephyr_library_sources(
  vl53l4cd.c
  vl53l4cd_platform.c
  ${VL53L4CD_CORE_SRC}/VL53L4CD_api.c
  ${VL53L4CD_CORE_SRC}/VL53L4CD_calibration.c
)