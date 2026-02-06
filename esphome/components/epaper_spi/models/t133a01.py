"""Driver for the T133A01 13.3" color e-paper display.

This display requires two chip select pins:
- cs_pin (standard): Used for data transfer
- cs2_pin (additional): Used for configuration commands

This implementation uses a workaround by manually controlling cs2_pin as a GPIO.
"""

from . import EpaperModel

CONF_CS2_PIN = "cs2_pin"


class T133A01Model(EpaperModel):
    def __init__(self, name, class_name="EPaperT133A01", **defaults):
        super().__init__(name, class_name, **defaults)

    def get_init_sequence(self, config: dict):
        """Return init sequence for T133A01 display.

        Based on official driver at:
        https://github.com/lilygo/T133A01/
        """
        width, height = self.get_dimensions(config)

        # Convert dimensions to bytes
        tres_width_h = width >> 8
        tres_width_l = width & 0xFF
        tres_height_h = height >> 8
        tres_height_l = height & 0xFF

        # fmt: off
        return (
            (0x74, 0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55),  # Unknown register
            (0xF0, 0x49, 0x55, 0x13, 0x5D, 0x05, 0x10),  # Unknown register
            (0x00, 0xDF, 0x69),  # PSR - Panel Setting Register
            (0x50, 0x37),  # CDI - VCOM and Data Interval Setting
            (0x60, 0x03, 0x03),  # TCON Setting
            (0x86, 0x10),  # Unknown register
            (0xE3, 0x22),  # PWS - Power Saving
            (0x61, tres_height_h, tres_height_l, tres_width_h, tres_width_l),  # TRES - Resolution Setting
            (0x01, 0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38),  # PWR - Power Setting
            (0xB6, 0x07),  # Unknown register
            (0x06, 0xD8, 0x18),  # BTST_P - Booster Soft Start (Positive)
            (0xB7, 0x01),  # Unknown register
            (0x05, 0xD8, 0x18),  # BTST_N - Booster Soft Start (Negative)
            (0xB0, 0x01),  # Unknown register
            (0xB1, 0x02),  # Unknown register
        )
        # fmt: on


# Create the T133A01 model instance
t133a01 = T133A01Model(
    "T133A01",
    width=1600,
    height=1200,
    minimum_update_interval="30s",
    data_rate="10MHz",
    cs2_pin=None,  # Required - second chip select for config commands
)
