#include "cs35l43.h"
#include "registers.h"

static ULONG Cs35l43DebugLevel = 100;
static ULONG Cs35l43DebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

NTSTATUS
DriverEntry(
	__in PDRIVER_OBJECT  DriverObject,
	__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	Cs35l43Print(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, Cs35l43EvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status))
	{
		Cs35l43Print(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

NTSTATUS cs35l43_reg_write(PCS35L43_CONTEXT pDevice, UINT32 reg, UINT32 data)
{
	UINT8 buf[8];

	buf[0] = (reg >> 24) & 0xFF;
	buf[1] = (reg >> 16) & 0xFF;
	buf[2] = (reg >> 8) & 0xFF;
	buf[3] = reg & 0xFF;
	buf[4] = (data >> 24) & 0xFF;
	buf[5] = (data >> 16) & 0xFF;
	buf[6] = (data >> 8) & 0xFF;
	buf[7] = data & 0xFF;

	return SpbWriteDataSynchronously(&pDevice->I2CContext, buf, sizeof(buf));
}

NTSTATUS cs35l43_reg_bulk_write(PCS35L43_CONTEXT pDevice, UINT32 reg, UINT8* data, UINT32 length)
{
	UINT8 buf[4];

	buf[0] = (reg >> 24) & 0xFF;
	buf[1] = (reg >> 16) & 0xFF;
	buf[2] = (reg >> 8) & 0xFF;
	buf[3] = reg & 0xFF;

	return SpbWriteDataSynchronouslyEx(&pDevice->I2CContext, buf, sizeof(buf), data, length);
}

NTSTATUS cs35l43_reg_read(
	_In_ PCS35L43_CONTEXT pDevice,
	UINT32 reg,
	UINT32* data
) {
	NTSTATUS status;
	UINT8 buf[4];

	buf[0] = (reg >> 24) & 0xFF;
	buf[1] = (reg >> 16) & 0xFF;
	buf[2] = (reg >> 8) & 0xFF;
	buf[3] = reg & 0xFF;

	UINT32 raw_data = 0;
	status = SpbWriteRead(&pDevice->I2CContext, &buf, sizeof(UINT32), &raw_data, sizeof(UINT32), 0);
	*data = RtlUlongByteSwap(raw_data);

	return status;
}

NTSTATUS cs35l43_reg_bulk_read(
	_In_ PCS35L43_CONTEXT pDevice,
	UINT32 reg,
	UINT32* data,
	UINT32 length
) {
	NTSTATUS status = STATUS_IO_DEVICE_ERROR;
	UINT8 buf[4];
	UINT32 raw_data;

	for (UINT32 i = 0; i < length; i++) {
		buf[0] = (reg >> 24) & 0xFF;
		buf[1] = (reg >> 16) & 0xFF;
		buf[2] = (reg >> 8) & 0xFF;
		buf[3] = reg & 0xFF;

		status = SpbWriteRead(&pDevice->I2CContext, &buf, sizeof(UINT32), &raw_data, sizeof(UINT32), 0);
		data[i] = RtlUlongByteSwap(raw_data);

		reg = reg + 4;
	}

	return status;
}

NTSTATUS cs35l43_reg_update_bits(PCS35L43_CONTEXT pDevice, UINT32 reg, UINT32 mask, UINT32 val)
{
	NTSTATUS status;
	UINT32 temp_val, data;

	status = cs35l43_reg_read(pDevice, reg, &data);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	temp_val = data & ~mask;
	temp_val |= val & mask;

	if (data == temp_val)
	{
		status = STATUS_SUCCESS;
		return status;
	}
	status = cs35l43_reg_write(pDevice, reg, temp_val);

	return status;
}

void udelay(ULONG usec) {
	LARGE_INTEGER Interval;
	Interval.QuadPart = -10 * (LONGLONG)usec;
	KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

void msleep(ULONG msec) {
	udelay(msec * 1000);
}

static NTSTATUS cs35l43_amp_enable(PCS35L43_CONTEXT pDevice)
{
	//pup patch
	cs35l43_reg_write(pDevice, CS35L43_TEST_KEY_CTRL, 0x00000055);
	cs35l43_reg_write(pDevice, CS35L43_TEST_KEY_CTRL, 0x000000AA);
	cs35l43_reg_write(pDevice, CS35L43_TST_OSC, 0x000F1AA0);
	cs35l43_reg_write(pDevice, CS35L43_TEST_KEY_CTRL, 0x000000CC);
	cs35l43_reg_write(pDevice, CS35L43_TEST_KEY_CTRL, 0x00000033);

	cs35l43_reg_write(pDevice, CS35L43_GLOBAL_ENABLES, 1);

	cs35l43_reg_update_bits(pDevice, CS35L43_BLOCK_ENABLES,
		CS35L43_AMP_EN_MASK, CS35L43_AMP_EN_MASK);

	return STATUS_SUCCESS;
}

static NTSTATUS cs35l43_amp_disable(PCS35L43_CONTEXT pDevice)
{
	cs35l43_reg_update_bits(pDevice, CS35L43_BLOCK_ENABLES,
		CS35L43_AMP_EN_MASK, 0);

	//pdn patch
	cs35l43_reg_write(pDevice, CS35L43_TEST_KEY_CTRL, 0x00000055);
	cs35l43_reg_write(pDevice, CS35L43_TEST_KEY_CTRL, 0x000000AA);
	cs35l43_reg_write(pDevice, CS35L43_TST_OSC, 0x000F1AA3);
	cs35l43_reg_write(pDevice, CS35L43_TEST_KEY_CTRL, 0x000000CC);
	cs35l43_reg_write(pDevice, CS35L43_TEST_KEY_CTRL, 0x00000033);

	msleep(1);

	cs35l43_reg_write(pDevice, CS35L43_GLOBAL_ENABLES, 0);

	return STATUS_SUCCESS;
}

static NTSTATUS cs35l43_component_set_sysclk(PCS35L43_CONTEXT pDevice)
{
	//1536000
	UINT32 fs1_val = 71, fs2_val = 115, val;
	UINT32 extclk_cfg = 0x1B;

	val = fs1_val;
	val |= (fs2_val << CS35L43_FS2_START_WINDOW_SHIFT) & CS35L43_FS2_START_WINDOW_MASK;
	cs35l43_reg_write(pDevice, CS35L43_FS_MON_0, val);

	cs35l43_reg_update_bits(pDevice, CS35L43_REFCLK_INPUT,
		CS35L43_PLL_OPEN_LOOP_MASK, CS35L43_PLL_OPEN_LOOP_MASK);
	cs35l43_reg_update_bits(pDevice, CS35L43_REFCLK_INPUT,
		CS35L43_PLL_REFCLK_FREQ_MASK,
		extclk_cfg << CS35L43_PLL_REFCLK_FREQ_SHIFT);
	cs35l43_reg_update_bits(pDevice, CS35L43_REFCLK_INPUT,
		CS35L43_PLL_REFCLK_EN_MASK, 0);
	cs35l43_reg_update_bits(pDevice, CS35L43_REFCLK_INPUT,
		CS35L43_PLL_REFCLK_SEL_MASK, 0); //SCLK
	cs35l43_reg_update_bits(pDevice, CS35L43_REFCLK_INPUT,
		CS35L43_PLL_OPEN_LOOP_MASK, 0);
	cs35l43_reg_update_bits(pDevice, CS35L43_REFCLK_INPUT,
		CS35L43_PLL_REFCLK_EN_MASK, CS35L43_PLL_REFCLK_EN_MASK);

	return STATUS_SUCCESS;
}

static NTSTATUS cs35l43_pcm_hw_params(PCS35L43_CONTEXT pDevice)
{
	INT32 asp_width = 32;
	INT32 asp_wl = 16;

	//48000
	cs35l43_reg_update_bits(pDevice, CS35L43_GLOBAL_SAMPLE_RATE,
		CS35L43_GLOBAL_FS_MASK,
		0x3);

	cs35l43_component_set_sysclk(pDevice);


	cs35l43_reg_update_bits(pDevice, CS35L43_ASP_CONTROL2,
		CS35L43_ASP_RX_WIDTH_MASK,
		asp_width << CS35L43_ASP_RX_WIDTH_SHIFT);
	cs35l43_reg_update_bits(pDevice, CS35L43_ASP_DATA_CONTROL5,
		CS35L43_ASP_RX_WL_MASK,
		asp_wl << CS35L43_ASP_RX_WL_SHIFT);


	if (pDevice->UID == 0 || pDevice->UID == 2 || pDevice->UID == 4) {
		cs35l43_reg_update_bits(pDevice,
			CS35L43_ASP_FRAME_CONTROL5,
			CS35L43_ASP_RX1_SLOT_MASK,
			0 << CS35L43_ASP_RX1_SLOT_SHIFT);
		Cs35l43Print(DEBUG_LEVEL_INFO, DBG_INIT,
			"Right Slot UID:%d\n", pDevice->UID);
	}
	else {
		cs35l43_reg_update_bits(pDevice,
			CS35L43_ASP_FRAME_CONTROL5,
			CS35L43_ASP_RX1_SLOT_MASK,
			1 << CS35L43_ASP_RX1_SLOT_SHIFT);
		Cs35l43Print(DEBUG_LEVEL_INFO, DBG_INIT,
			"Left Slot UID:%d\n", pDevice->UID);
	}

	return STATUS_SUCCESS;
}

static NTSTATUS cs35l43_set_dai_fmt(PCS35L43_CONTEXT pDevice)
{
	INT32 sclk_fmt = 0; //SND_SOC_DAIFMT_NB_NF
	INT32 lrclk_fmt = 0; //SND_SOC_DAIFMT_NB_NF
	INT32 asp_fmt = 2; //I2S

	cs35l43_reg_update_bits(pDevice, CS35L43_ASP_CONTROL2,
		CS35L43_ASP_FMT_MASK | CS35L43_ASP_BCLK_INV_MASK |
		CS35L43_ASP_FSYNC_INV_MASK,
		(asp_fmt << CS35L43_ASP_FMT_SHIFT) |
		(lrclk_fmt << CS35L43_ASP_FSYNC_INV_SHIFT) |
		(sclk_fmt << CS35L43_ASP_BCLK_INV_SHIFT));

	return STATUS_SUCCESS;
}

NTSTATUS
GetDeviceUID(
	_In_ WDFDEVICE FxDevice,
	_In_ PINT32 PUID
)
{
	NTSTATUS status = STATUS_ACPI_NOT_INITIALIZED;
	ACPI_EVAL_INPUT_BUFFER_EX inputBuffer;
	RtlZeroMemory(&inputBuffer, sizeof(inputBuffer));

	inputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE_EX;
	status = RtlStringCchPrintfA(
		inputBuffer.MethodName,
		sizeof(inputBuffer.MethodName),
		"_UID"
	);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	WDFMEMORY outputMemory;
	PACPI_EVAL_OUTPUT_BUFFER outputBuffer;
	size_t outputArgumentBufferSize = 32;
	size_t outputBufferSize = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + outputArgumentBufferSize;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	attributes.ParentObject = FxDevice;

	status = WdfMemoryCreate(&attributes,
		NonPagedPoolNx,
		0,
		outputBufferSize,
		&outputMemory,
		(PVOID*)&outputBuffer);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	RtlZeroMemory(outputBuffer, outputBufferSize);

	WDF_MEMORY_DESCRIPTOR inputMemDesc;
	WDF_MEMORY_DESCRIPTOR outputMemDesc;
	WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&inputMemDesc, &inputBuffer, (ULONG)sizeof(inputBuffer));
	WDF_MEMORY_DESCRIPTOR_INIT_HANDLE(&outputMemDesc, outputMemory, NULL);

	status = WdfIoTargetSendInternalIoctlSynchronously(
		WdfDeviceGetIoTarget(FxDevice),
		NULL,
		IOCTL_ACPI_EVAL_METHOD_EX,
		&inputMemDesc,
		&outputMemDesc,
		NULL,
		NULL
	);
	if (!NT_SUCCESS(status)) {
		goto Exit;
	}

	if (outputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) {
		goto Exit;
	}

	if (outputBuffer->Count < 1) {
		goto Exit;
	}

	UINT32 uid;
	if (outputBuffer->Argument[0].DataLength >= 4) {
		uid = *(UINT32*)outputBuffer->Argument->Data;
	}
	else if (outputBuffer->Argument[0].DataLength >= 2) {
		uid = *(UINT16*)outputBuffer->Argument->Data;
	}
	else {
		uid = *(UINT8*)outputBuffer->Argument->Data;
	}
	if (PUID) {
		*PUID = uid;
	}
	else {
		status = STATUS_ACPI_INVALID_ARGUMENT;
	}
Exit:
	if (outputMemory != WDF_NO_HANDLE) {
		WdfObjectDelete(outputMemory);
	}
	return status;
}

BOOLEAN
OnInterruptIsr(
	IN WDFINTERRUPT Interrupt,
	IN ULONG MessageID
)
{
	PCS35L43_CONTEXT pDevice;
	BOOLEAN ret;

	UNREFERENCED_PARAMETER(MessageID);

	Cs35l43Print(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"OnInterruptIsr - Entry");

	ret = FALSE;
	pDevice = GetDeviceContext(WdfInterruptGetDevice(Interrupt));

	UINT32 status[2] = { 0, 0 };
	UINT32 masks[2] = { 0, 0 };
	UINT32 i;

	for (i = 0; i < sizeof(status) / sizeof(status[0]); i++) {
		cs35l43_reg_read(pDevice,
			CS35L43_IRQ1_EINT_1 + (i * 4),
			&status[i]);
		cs35l43_reg_read(pDevice,
			CS35L43_IRQ1_MASK_1 + (i * 4),
			&masks[i]);
	}

	/* Check to see if unmasked bits are active */
	if (!(status[0] & ~masks[0]) && !(status[1] & ~masks[1]))
		goto done;

	/*
	 * The following interrupts require a
	 * protection release cycle to get the
	 * speaker out of Safe-Mode.
	 */
	if (status[0] & CS35L43_AMP_ERR_EINT1_MASK) {
		Cs35l43Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "Amp short error\n");
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_1,
			CS35L43_AMP_ERR_EINT1_MASK);
		cs35l43_reg_write(pDevice, CS35L43_ERROR_RELEASE, 0);
		cs35l43_reg_update_bits(pDevice, CS35L43_ERROR_RELEASE,
			CS35L43_AMP_SHORT_ERR_RLS_MASK,
			CS35L43_AMP_SHORT_ERR_RLS_MASK);
		cs35l43_reg_update_bits(pDevice, CS35L43_ERROR_RELEASE,
			CS35L43_AMP_SHORT_ERR_RLS_MASK, 0);
		ret = TRUE;
	}

	if (status[0] & CS35L43_BST_OVP_ERR_EINT1_MASK) {
		Cs35l43Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "VBST Over Voltage error\n");
		cs35l43_reg_update_bits(pDevice, CS35L43_BLOCK_ENABLES,
			CS35L43_BST_EN_MASK << CS35L43_BST_EN_SHIFT, 0);
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_1,
			CS35L43_BST_OVP_ERR_EINT1_MASK);
		cs35l43_reg_write(pDevice, CS35L43_ERROR_RELEASE, 0);
		cs35l43_reg_update_bits(pDevice, CS35L43_ERROR_RELEASE,
			CS35L43_BST_OVP_ERR_RLS_MASK,
			CS35L43_BST_OVP_ERR_RLS_MASK);
		cs35l43_reg_update_bits(pDevice, CS35L43_ERROR_RELEASE,
			CS35L43_BST_OVP_ERR_RLS_MASK, 0);
		cs35l43_reg_update_bits(pDevice, CS35L43_BLOCK_ENABLES,
			CS35L43_BST_EN_MASK << CS35L43_BST_EN_SHIFT,
			CS35L43_BST_EN_DEFAULT << CS35L43_BST_EN_SHIFT);
		ret = TRUE;
	}

	if (status[0] & CS35L43_BST_DCM_UVP_ERR_EINT1_MASK) {
		Cs35l43Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "DCM VBST Under Voltage Error\n");
		cs35l43_reg_update_bits(pDevice, CS35L43_BLOCK_ENABLES,
			CS35L43_BST_EN_MASK << CS35L43_BST_EN_SHIFT, 0);
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_1,
			CS35L43_BST_DCM_UVP_ERR_EINT1_MASK);
		cs35l43_reg_write(pDevice, CS35L43_ERROR_RELEASE, 0);
		cs35l43_reg_update_bits(pDevice, CS35L43_ERROR_RELEASE,
			CS35L43_BST_UVP_ERR_RLS_MASK,
			CS35L43_BST_UVP_ERR_RLS_MASK);
		cs35l43_reg_update_bits(pDevice, CS35L43_ERROR_RELEASE,
			CS35L43_BST_UVP_ERR_RLS_MASK, 0);
		cs35l43_reg_update_bits(pDevice, CS35L43_BLOCK_ENABLES,
			CS35L43_BST_EN_MASK << CS35L43_BST_EN_SHIFT,
			CS35L43_BST_EN_DEFAULT << CS35L43_BST_EN_SHIFT);
		ret = TRUE;
	}

	if (status[0] & CS35L43_BST_SHORT_ERR_EINT1_MASK) {
		Cs35l43Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "LBST error: powering off!\n");
		cs35l43_reg_update_bits(pDevice, CS35L43_BLOCK_ENABLES,
			CS35L43_BST_EN_MASK << CS35L43_BST_EN_SHIFT, 0);
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_1,
			CS35L43_BST_SHORT_ERR_EINT1_MASK);
		cs35l43_reg_write(pDevice, CS35L43_ERROR_RELEASE, 0);
		cs35l43_reg_update_bits(pDevice, CS35L43_ERROR_RELEASE,
			CS35L43_BST_SHORT_ERR_RLS_MASK,
			CS35L43_BST_SHORT_ERR_RLS_MASK);
		cs35l43_reg_update_bits(pDevice, CS35L43_ERROR_RELEASE,
			CS35L43_BST_SHORT_ERR_RLS_MASK, 0);
		cs35l43_reg_update_bits(pDevice, CS35L43_BLOCK_ENABLES,
			CS35L43_BST_EN_MASK << CS35L43_BST_EN_SHIFT,
			CS35L43_BST_EN_DEFAULT << CS35L43_BST_EN_SHIFT);
		ret = TRUE;
	}

	if (status[0] & CS35L43_DC_WATCHDOG_IRQ_RISE_EINT1_MASK) {
		Cs35l43Print(DEBUG_LEVEL_ERROR, DBG_IOCTL, "DC Detect INT\n");
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_1,
			CS35L43_DC_WATCHDOG_IRQ_RISE_EINT1_MASK);
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_1,
			CS35L43_DC_WATCHDOG_IRQ_RISE_EINT1_MASK);
		ret = TRUE;
	}

	if (status[0] & CS35L43_WKSRC_STATUS_ANY_EINT1_MASK ||
		status[0] & CS35L43_WKSRC_STATUS6_EINT1_MASK) {
		Cs35l43Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "Wakeup INT\n");
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_1,
			CS35L43_WKSRC_STATUS_ANY_EINT1_MASK);
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_1,
			CS35L43_WKSRC_STATUS6_EINT1_MASK);
		ret = TRUE;
	}

	if (status[1] & CS35L43_PLL_UNLOCK_FLAG_RISE_EINT1_MASK) {
		Cs35l43Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "PLL Unlock INT\n");
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_2,
			CS35L43_PLL_UNLOCK_FLAG_RISE_EINT1_MASK);
		ret = TRUE;
	}

	if (status[1] & CS35L43_PLL_LOCK_EINT1_MASK) {
		Cs35l43Print(DEBUG_LEVEL_INFO, DBG_IOCTL, "PLL Lock INT\n");
		cs35l43_reg_write(pDevice, CS35L43_IRQ1_EINT_2,
			CS35L43_PLL_LOCK_EINT1_MASK);
		ret = TRUE;
	}

done:

	return ret;
}

NTSTATUS
StartCodec(
	PCS35L43_CONTEXT pDevice
) {
	NTSTATUS status = STATUS_SUCCESS;
	if (!pDevice->SetUID) {
		status = STATUS_ACPI_INVALID_DATA;
		return status;
	}

	UINT32 revid = 0;
	status = cs35l43_reg_read(pDevice, CS35L43_DEVID, &revid);
	if (!NT_SUCCESS(status)) {
		Cs35l43Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Failed to read device ID\n");
		status = STATUS_IO_DEVICE_ERROR;
		return status;
	}

	if (revid != CS35L43_CHIP_ID) {
		Cs35l43Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Invalid device id (0x%x), expected 0x%x\n", revid, CS35L43_CHIP_ID);
		status = STATUS_IO_DEVICE_ERROR;
		return status;
	}

	UINT32 reg_revid = 0;
	status = cs35l43_reg_read(pDevice, CS35L43_REVID, &reg_revid);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	Cs35l43Print(DEBUG_LEVEL_INFO, DBG_PNP,
		"CS35L43 revision 0x%02x\n", reg_revid);

	//Errata
	cs35l43_reg_write(pDevice, CS35L43_TST_DAC_MSM_CONFIG, 0x11330000);
	cs35l43_reg_write(pDevice, CS35L43_BST_RSVD_1, 0x50000802);

	//cirrus,gpio2-src-sel = 4(irq active low)
	cs35l43_reg_update_bits(pDevice, CS35L43_GPIO_PAD_CONTROL,
		CS35L43_GP2_CTRL_MASK, 4 << CS35L43_GP2_CTRL_SHIFT);

	cs35l43_reg_write(pDevice, CS35L43_IRQ1_MASK_1, 0xFFFFFFFF);
	cs35l43_reg_update_bits(pDevice, CS35L43_IRQ1_MASK_1,
		CS35L43_DC_WATCHDOG_IRQ_RISE_EINT1_MASK |
		CS35L43_AMP_ERR_EINT1_MASK |
		CS35L43_BST_SHORT_ERR_EINT1_MASK |
		CS35L43_BST_DCM_UVP_ERR_EINT1_MASK |
		CS35L43_BST_OVP_ERR_EINT1_MASK |
		CS35L43_WKSRC_STATUS6_EINT1_MASK |
		CS35L43_WKSRC_STATUS_ANY_EINT1_MASK, 0);
	cs35l43_reg_write(pDevice, CS35L43_IRQ1_MASK_2, 0xFFFFFFFF);
	cs35l43_reg_update_bits(pDevice, CS35L43_IRQ1_MASK_2,
		CS35L43_PLL_UNLOCK_FLAG_RISE_EINT1_MASK |
		CS35L43_PLL_LOCK_EINT1_MASK, 0);
	cs35l43_reg_write(pDevice, CS35L43_IRQ1_MASK_3, 0xFFFFFFFF);

	cs35l43_reg_update_bits(pDevice, CS35L43_ALIVE_DCIN_WD,
		CS35L43_DCIN_WD_EN_MASK, 1);
	cs35l43_reg_update_bits(pDevice, CS35L43_ALIVE_DCIN_WD,
		CS35L43_DCIN_WD_THLD_MASK, 1 << CS35L43_DCIN_WD_THLD_SHIFT);

	//cirrus,bst-ipk-ma = 4000
	//((cirrus,bst-ipk-ma - 1600) / 50) + 16
	cs35l43_reg_update_bits(pDevice, CS35L43_BST_IPK_CTL,
		CS35L43_BST_IPK_MASK, 64);

	//cirrus,asp-sdout-hiz = 3
	cs35l43_reg_update_bits(pDevice, CS35L43_ASP_CONTROL3,
		CS35L41_ASP_DOUT_HIZ_CTRL_MASK, 3);

	//cirrus,vpbr-rel-rate = 0
	cs35l43_reg_update_bits(pDevice, CS35L43_VPBR_CONFIG,
		CS35L43_VPBR_REL_RATE_MASK, 0 << CS35L43_VPBR_REL_RATE_SHIFT);

	//cirrus,vpbr-wait = 0
	cs35l43_reg_update_bits(pDevice, CS35L43_VPBR_CONFIG,
		CS35L43_VPBR_WAIT_MASK, 0 << CS35L43_VPBR_WAIT_SHIFT);

	//cirrus,vpbr-atk-rate = 0
	cs35l43_reg_update_bits(pDevice, CS35L43_VPBR_CONFIG,
		CS35L43_VPBR_ATK_RATE_MASK, 0 << CS35L43_VPBR_ATK_RATE_SHIFT);

	//cirrus,vpbr-atk-vol = 5
	cs35l43_reg_update_bits(pDevice, CS35L43_VPBR_CONFIG,
		CS35L43_VPBR_ATK_VOL_MASK, 5 << CS35L43_VPBR_ATK_VOL_SHIFT);

	//cirrus,vpbr-max-att = 3
	cs35l43_reg_update_bits(pDevice, CS35L43_VPBR_CONFIG,
		CS35L43_VPBR_MAX_ATT_MASK, 3 << CS35L43_VPBR_MAX_ATT_SHIFT);

	//cirrus,vpbr-thld = 8
	cs35l43_reg_update_bits(pDevice, CS35L43_VPBR_CONFIG,
		CS35L43_VPBR_THLD1_MASK, 8 << CS35L43_VPBR_THLD1_SHIFT);

	//cirrus,vpbr-enable = 1
	cs35l43_reg_update_bits(pDevice, CS35L43_BLOCK_ENABLES2,
		CS35L43_VPBR_EN_MASK, CS35L43_VPBR_EN_MASK);

	cs35l43_set_dai_fmt(pDevice);

	cs35l43_pcm_hw_params(pDevice);

	cs35l43_reg_update_bits(pDevice, CS35L43_AMP_CTRL,
		CS35L43_AMP_VOL_PCM_MASK,
		4096 << CS35L43_AMP_VOL_PCM_SHIFT);
	cs35l43_reg_update_bits(pDevice, CS35L43_AMP_CTRL,
		CS35L43_AMP_RAMP_PCM_MASK, 4);


	cs35l43_reg_update_bits(pDevice, CS35L43_ASP_ENABLES1,
		CS35L43_ASP_RX1_EN_MASK, CS35L43_ASP_RX1_EN_MASK);

	cs35l43_reg_write(pDevice, CS35L43_DACPCM1_INPUT, CS35L43_INPUT_SRC_ASPRX1);

	//cs35l43_reg_update_bits(pDevice, CS35L43_AMP_GAIN,
	//	CS35L43_AMP_GAIN_PCM_MASK, 18 << CS35L43_AMP_GAIN_PCM_SHIFT);
	cs35l43_reg_update_bits(pDevice, CS35L43_AMP_GAIN,
		CS35L43_AMP_GAIN_PCM_MASK, 10 << CS35L43_AMP_GAIN_PCM_SHIFT);

	cs35l43_amp_enable(pDevice);

	Cs35l43Print(DEBUG_LEVEL_INFO, DBG_INIT,
		"Started! REV:%X UID:%d\n", reg_revid, pDevice->UID);

	pDevice->DevicePoweredOn = TRUE;
	return status;
}

NTSTATUS
StopCodec(
	PCS35L43_CONTEXT pDevice
) {
	NTSTATUS status = STATUS_SUCCESS;

	status = cs35l43_amp_disable(pDevice);

	pDevice->DevicePoweredOn = FALSE;
	return status;
}

NTSTATUS
OnPrepareHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesRaw,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PCS35L43_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:
			//
			// Look for I2C or SPI resource and save connection ID.
			//
			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;
			if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				if (fSpbResourceFound == FALSE)
				{
					status = STATUS_SUCCESS;
					pDevice->I2CContext.I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
					pDevice->I2CContext.I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;
					fSpbResourceFound = TRUE;
				}
				else
				{
				}
			}
			break;
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
	}

	status = SpbTargetInitialize(FxDevice, &pDevice->I2CContext);

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	status = GetDeviceUID(FxDevice, &pDevice->UID);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	pDevice->SetUID = TRUE;

	return status;
}

NTSTATUS
OnReleaseHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PCS35L43_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	SpbTargetDeinitialize(FxDevice, &pDevice->I2CContext);

	return status;
}

NTSTATUS
OnD0Entry(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PCS35L43_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	status = StartCodec(pDevice);

	return status;
}

NTSTATUS
OnD0Exit(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PCS35L43_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	status = StopCodec(pDevice);

	return STATUS_SUCCESS;
}

NTSTATUS
Cs35l43EvtDeviceAdd(
	IN WDFDRIVER       Driver,
	IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_INTERRUPT_CONFIG          interruptConfig;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	PCS35L43_CONTEXT              devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	Cs35l43Print(DEBUG_LEVEL_INFO, DBG_PNP,
		"Cs35l43EvtDeviceAdd called\n");

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, CS35L43_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		Cs35l43Print(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	devContext = GetDeviceContext(device);
	devContext->FxDevice = device;

	//
	// Create an interrupt object for hardware notifications
	//
	WDF_INTERRUPT_CONFIG_INIT(
		&interruptConfig,
		OnInterruptIsr,
		NULL);
	interruptConfig.PassiveHandling = TRUE;

	status = WdfInterruptCreate(
		device,
		&interruptConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->InterruptObject);

	return status;
}
