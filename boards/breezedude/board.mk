CHIP_FAMILY = samd21
CHIP_VARIANT = SAMD21G18A
# Set to 1 to include the chip UUID in INFO_UF2.TXT (increases code size slightly).
# Disabled by default; UUID is written to VERSIONS.TXT by the app instead.
BLD_EXTA_FLAGS += -DUSE_INFO_UF2=0
