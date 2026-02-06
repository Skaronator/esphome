from . import EpaperModel


class T133A01Model(EpaperModel):
    def __init__(self, name, **defaults):
        super().__init__(name, "EPaperT133A01", **defaults)

    def get_init_sequence(self, config: dict):
        """
        Generate init sequence for T133A01 display based on Arduino reference.
        All register values must match complete_source.cpp for display to work!

        NOTE: PON (0x04) is NOT sent during init - only during update sequence.
        The manufacturer's EPD_INIT does not include PON, only EPD_UPDATE does.
        """
        width, height = self.get_dimensions(config)

        # T133A01 initialization sequence (from complete_source.cpp lines 136059-136139)
        return (
            # r74 - Waveform/LUT configuration (9 bytes from Arduino)
            (0x74, 0xC0, 0x1C, 0x1C, 0xCC, 0xCC, 0xCC, 0x15, 0x15, 0x55),
            # PSR - Panel Setting Register
            (0x00, 0xDF, 0x69),
            # PWR - Power Setting Register (6 bytes: VGH, VGL, VSH, VSL settings)
            (0x01, 0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38),
            # CDI - Color/Component Driving Interface
            (0x50, 0x37),
            # r60 - TCON Driving Mode (corrected to match Arduino)
            (0x60, 0x03, 0x03),
            # TRES - Resolution
            (0x61, width >> 8, width & 0xFF, height >> 8, height & 0xFF),
            # r86 - Timing Control
            (0x86, 0x01),
            # rf0 - Advanced timing configuration (6 bytes from Arduino)
            (0xF0, 0x49, 0x55, 0x13, 0x5D, 0x05, 0x10),
            # E3 - Power Settings
            (0xE3, 0x2F),
            # rb6 - Border Waveform
            (0xB6, 0x0D),
            # rb7 - Border Control
            (0xB7, 0x0D),
            # rb0 - Power On Sequence
            (0xB0, 0x00),
            # rb1 - Panel Breaking
            (0xB1, 0x00),
            # BTST - Booster Soft Start (send both positive and negative)
            (0x06, 0xD8, 0x18),
            (0x06, 0xD8, 0x18),
            # NO PON here - manufacturer's EPD_INIT doesn't include it
            # PON is sent during EPD_UPDATE after data transfer
        )

    def get_constructor_args(self, config) -> tuple:
        return ()


t133a01_base = T133A01Model(
    "t133a01",
    width=1600,
    height=1200,
    data_rate="10MHz",
)
