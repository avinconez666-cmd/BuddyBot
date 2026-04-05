package com.buddybot.kids

class SecurityGatekeeper(
    private val sendSerial: (String) -> Unit,
    private val onTransition: (RobotMode) -> Unit
) {
    private val MASTER_PIN = "1234"

    fun processCommand(input: String) {
        // Expected format from Mega/R4: "REQ_MODE_CHANGE:BODYGUARD:1234"
        val parts = input.split(":")
        if (parts.size == 3 && parts[0] == "REQ_MODE_CHANGE") {
            val targetModeStr = parts[1]
            val pin = parts[2]
            
            val targetMode = try {
                RobotMode.valueOf(targetModeStr)
            } catch (e: IllegalArgumentException) {
                null
            }

            if (targetMode != null && pin == MASTER_PIN) {
                onTransition(targetMode)
                sendSerial("UNLOCK_OK")
            } else {
                sendSerial("ACCESS_DENIED")
            }
        }
    }

    fun validatePin(pin: String, mode: RobotMode): Boolean {
        return if (pin == MASTER_PIN) {
            onTransition(mode)
            sendSerial("UNLOCK_OK")
            true
        } else {
            sendSerial("ACCESS_DENIED")
            false
        }
    }
}
