"""Errors the builder reports to the player in plain language."""


class BuilderError(Exception):
    """A problem the player can act on. `str(error)` is the message to show.

    `code` is a stable machine-readable identifier (for example "rom_modified")
    that front ends other than the window, such as the web builder, use to pick
    their own wording; it never changes the message itself."""

    def __init__(self, message: str, hint: str = "", code: str = "builder_error"):
        super().__init__(message)
        self.message = message
        self.hint = hint
        self.code = code

    def __str__(self) -> str:
        return self.message + ("\n\n" + self.hint if self.hint else "")
