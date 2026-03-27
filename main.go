//go:build windows

package main

import (
	"bytes"
	_ "embed"
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/gif"
	"math/rand"
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"syscall"
	"time"
	"unsafe"

	"golang.org/x/sys/windows/registry"
)

// Embed the hologram GIF image
//
//go:embed hologram/Resources/yy3.gif
var gifData []byte

// ============================================================
// Win32 DLL & Proc declarations
// ============================================================

var (
	user32   = syscall.NewLazyDLL("user32.dll")
	kernel32 = syscall.NewLazyDLL("kernel32.dll")
	gdi32    = syscall.NewLazyDLL("gdi32.dll")

	pRegisterClassExW           = user32.NewProc("RegisterClassExW")
	pCreateWindowExW            = user32.NewProc("CreateWindowExW")
	pDefWindowProcW             = user32.NewProc("DefWindowProcW")
	pGetMessageW                = user32.NewProc("GetMessageW")
	pTranslateMessage           = user32.NewProc("TranslateMessage")
	pDispatchMessageW           = user32.NewProc("DispatchMessageW")
	pPostQuitMessage            = user32.NewProc("PostQuitMessage")
	pShowWindow                 = user32.NewProc("ShowWindow")
	pUpdateWindow               = user32.NewProc("UpdateWindow")
	pSetWindowPos               = user32.NewProc("SetWindowPos")
	pSetTimer                   = user32.NewProc("SetTimer")
	pSendMessageW               = user32.NewProc("SendMessageW")
	pReleaseCapture             = user32.NewProc("ReleaseCapture")
	pMouseEvent                 = user32.NewProc("mouse_event")
	pBeginPaint                 = user32.NewProc("BeginPaint")
	pEndPaint                   = user32.NewProc("EndPaint")
	pCreatePopupMenu            = user32.NewProc("CreatePopupMenu")
	pAppendMenuW                = user32.NewProc("AppendMenuW")
	pTrackPopupMenu             = user32.NewProc("TrackPopupMenu")
	pDestroyMenu                = user32.NewProc("DestroyMenu")
	pGetCursorPos               = user32.NewProc("GetCursorPos")
	pSetForegroundWindow        = user32.NewProc("SetForegroundWindow")
	pSetLayeredWindowAttributes = user32.NewProc("SetLayeredWindowAttributes")
	pLoadCursorW                = user32.NewProc("LoadCursorW")
	pGetWindowRect              = user32.NewProc("GetWindowRect")
	pDestroyWindow              = user32.NewProc("DestroyWindow")
	pInvalidateRect             = user32.NewProc("InvalidateRect")
	pKillTimer                  = user32.NewProc("KillTimer")

	pGetModuleHandleW = kernel32.NewProc("GetModuleHandleW")

	pCreateCompatibleDC     = gdi32.NewProc("CreateCompatibleDC")
	pCreateCompatibleBitmap = gdi32.NewProc("CreateCompatibleBitmap")
	pDeleteDC               = gdi32.NewProc("DeleteDC")
	pSelectObject           = gdi32.NewProc("SelectObject")
	pBitBlt                 = gdi32.NewProc("BitBlt")
	pStretchBlt             = gdi32.NewProc("StretchBlt")
	pDeleteObject           = gdi32.NewProc("DeleteObject")
	pCreateSolidBrush       = gdi32.NewProc("CreateSolidBrush")
	pSetStretchBltMode      = gdi32.NewProc("SetStretchBltMode")
	pCreateDIBSection       = gdi32.NewProc("CreateDIBSection")
	pGetDC                  = user32.NewProc("GetDC")
	pReleaseDC              = user32.NewProc("ReleaseDC")
)

// ============================================================
// Win32 constants
// ============================================================

const (
	csHRedraw = 0x0002
	csVRedraw = 0x0001

	wsPopup   = 0x80000000
	wsVisible = 0x10000000

	wsExLayered    = 0x00080000
	wsExTopmost    = 0x00000008
	wsExToolwindow = 0x00000080

	wmCreate        = 0x0001
	wmDestroy       = 0x0002
	wmPaint         = 0x000F
	wmEraseBkgnd    = 0x0014
	wmTimer         = 0x0113
	wmLButtonDown   = 0x0201
	wmRButtonDown   = 0x0204
	wmNCLButtonDown = 0x00A1

	htCaption = 0x0002
	swShow    = 5

	swpNoMove = 0x0002
	swpNoSize = 0x0001

	mouseeventfMove = 0x0001

	mfString  = 0x00000000
	mfChecked = 0x00000008

	tpmLeftAlign = 0x0000
	tpmReturnCmd = 0x0100
	tpmNoNotify  = 0x0080

	lwaColorKey = 0x00000001

	srcCopy  = 0x00CC0020
	halftone = 4

	idcArrow = 32512

	timerJiggle    = 1
	timerAnimation = 2

	idmStartWithWindows = 1001
	idmExit             = 1002
)

// LimeGreen RGB(50, 205, 50) in COLORREF format (0x00BBGGRR)
var colorLimeGreen = uintptr(0x0032CD32)

// HWND_TOPMOST = (HWND)-1
var hwndTopmost = ^uintptr(0)

// ============================================================
// Win32 structures
// ============================================================

type wndClassExW struct {
	CbSize        uint32
	Style         uint32
	LpfnWndProc   uintptr
	CbClsExtra    int32
	CbWndExtra    int32
	HInstance     uintptr
	HIcon         uintptr
	HCursor       uintptr
	HbrBackground uintptr
	LpszMenuName  *uint16
	LpszClassName *uint16
	HIconSm       uintptr
}

type point struct {
	X, Y int32
}

type rect struct {
	Left, Top, Right, Bottom int32
}

type msg struct {
	Hwnd    uintptr
	Message uint32
	WParam  uintptr
	LParam  uintptr
	Time    uint32
	Pt      point
}

type paintStruct struct {
	Hdc         uintptr
	FErase      int32
	RcPaint     rect
	FRestore    int32
	FIncUpdate  int32
	RgbReserved [32]byte
}

type bitmapInfoHeader struct {
	BiSize          uint32
	BiWidth         int32
	BiHeight        int32
	BiPlanes        uint16
	BiBitCount      uint16
	BiCompression   uint32
	BiSizeImage     uint32
	BiXPelsPerMeter int32
	BiYPelsPerMeter int32
	BiClrUsed       uint32
	BiClrImportant  uint32
}

// ============================================================
// Global state
// ============================================================

var (
	mainHwnd         uintptr
	hBitmap          uintptr
	imgWidth         int32
	imgHeight        int32
	startWithWindows bool
	windowSize       int32 = 120

	// Animation
	frameBitmaps []uintptr // Pre-scaled HBitmap per frame (windowSize x windowSize)
	frameDelays  []int     // delay in ms per frame
	currentFrame int
)

const (
	regKeyPath   = `SOFTWARE\Microsoft\Windows\CurrentVersion\Run`
	regValueName = "hologram-go"
)

// ============================================================
// Image loading
// ============================================================

func loadImage() {
	g, err := gif.DecodeAll(bytes.NewReader(gifData))
	if err != nil {
		panic("Failed to decode embedded GIF: " + err.Error())
	}

	bounds := image.Rect(0, 0, g.Config.Width, g.Config.Height)
	imgWidth = int32(bounds.Dx())
	imgHeight = int32(bounds.Dy())

	limeGreen := color.RGBA{R: 50, G: 205, B: 50, A: 255}
	transparent := color.RGBA{0, 0, 0, 0}

	// Composite canvas for proper GIF disposal
	canvas := image.NewRGBA(bounds)
	// Backup canvas for DisposalPrevious
	backup := image.NewRGBA(bounds)

	frameBitmaps = make([]uintptr, len(g.Image))
	frameDelays = make([]int, len(g.Image))

	for i, frame := range g.Image {
		// Save canvas state before drawing (for DisposalPrevious)
		copy(backup.Pix, canvas.Pix)

		// Draw current frame onto canvas
		draw.Draw(canvas, frame.Bounds(), frame, frame.Bounds().Min, draw.Over)

		// Create a copy with LimeGreen background for transparent pixels
		rgba := image.NewRGBA(bounds)
		draw.Draw(rgba, bounds, &image.Uniform{limeGreen}, image.Point{}, draw.Src)
		draw.Draw(rgba, bounds, canvas, bounds.Min, draw.Over)

		frameBitmaps[i] = createPreScaledBitmap(rgba)

		// GIF delay is in 100ths of a second; convert to ms
		delay := 100 // default 100ms
		if i < len(g.Delay) && g.Delay[i] > 0 {
			delay = g.Delay[i] * 10
		}
		frameDelays[i] = delay

		// Handle disposal method for NEXT frame rendering
		disposal := byte(gif.DisposalNone)
		if i < len(g.Disposal) {
			disposal = g.Disposal[i]
		}
		switch disposal {
		case gif.DisposalBackground:
			// Clear the frame area to transparent
			draw.Draw(canvas, frame.Bounds(), &image.Uniform{transparent}, image.Point{}, draw.Src)
		case gif.DisposalPrevious:
			// Restore canvas to state before this frame was drawn
			copy(canvas.Pix, backup.Pix)
			// DisposalNone (0) or unspecified: leave canvas as-is
		}
	}

	hBitmap = frameBitmaps[0]
	currentFrame = 0
}

func createHBitmapFromRGBA(rgba *image.RGBA) uintptr {
	bounds := rgba.Bounds()
	w := int32(bounds.Dx())
	h := int32(bounds.Dy())

	bmi := bitmapInfoHeader{
		BiSize:     uint32(unsafe.Sizeof(bitmapInfoHeader{})),
		BiWidth:    w,
		BiHeight:   -h, // negative = top-down DIB
		BiPlanes:   1,
		BiBitCount: 32,
	}

	var bits unsafe.Pointer
	hbm, _, _ := pCreateDIBSection.Call(
		0,
		uintptr(unsafe.Pointer(&bmi)),
		0, // DIB_RGB_COLORS
		uintptr(unsafe.Pointer(&bits)),
		0, 0,
	)
	if hbm == 0 {
		panic("CreateDIBSection failed")
	}

	// Copy pixels: Go RGBA → Windows BGRA
	src := rgba.Pix
	dst := unsafe.Slice((*byte)(bits), len(src))
	for i := 0; i < len(src); i += 4 {
		dst[i+0] = src[i+2] // B
		dst[i+1] = src[i+1] // G
		dst[i+2] = src[i+0] // R
		dst[i+3] = src[i+3] // A
	}

	return hbm
}

// createPreScaledBitmap creates an HBITMAP pre-scaled to windowSize x windowSize
// so onPaint can use fast BitBlt instead of StretchBlt.
func createPreScaledBitmap(rgba *image.RGBA) uintptr {
	srcBmp := createHBitmapFromRGBA(rgba)

	// Get a screen DC to create compatible objects
	screenDC, _, _ := pGetDC.Call(0)

	// Create source DC with the original bitmap
	srcDC, _, _ := pCreateCompatibleDC.Call(screenDC)
	oldSrc, _, _ := pSelectObject.Call(srcDC, srcBmp)

	// Create destination DC with a new bitmap at window size
	dstBmp, _, _ := pCreateCompatibleBitmap.Call(screenDC, uintptr(windowSize), uintptr(windowSize))
	dstDC, _, _ := pCreateCompatibleDC.Call(screenDC)
	oldDst, _, _ := pSelectObject.Call(dstDC, dstBmp)

	// High-quality stretch
	pSetStretchBltMode.Call(dstDC, halftone)
	pStretchBlt.Call(
		dstDC, 0, 0, uintptr(windowSize), uintptr(windowSize),
		srcDC, 0, 0, uintptr(imgWidth), uintptr(imgHeight),
		srcCopy,
	)

	// Cleanup
	pSelectObject.Call(srcDC, oldSrc)
	pDeleteDC.Call(srcDC)
	pDeleteObject.Call(srcBmp)

	pSelectObject.Call(dstDC, oldDst)
	pDeleteDC.Call(dstDC)
	pReleaseDC.Call(0, screenDC)

	return dstBmp
}

// ============================================================
// Window procedure
// ============================================================

func wndProc(hwnd, uMsg, wParam, lParam uintptr) uintptr {
	switch uMsg {
	case wmEraseBkgnd:
		// Prevent background erase to avoid flicker
		return 1

	case wmPaint:
		return onPaint(hwnd)

	case wmLButtonDown:
		// Enable dragging: simulate title bar click
		pReleaseCapture.Call()
		pSendMessageW.Call(hwnd, wmNCLButtonDown, htCaption, 0)
		return 0

	case wmRButtonDown:
		showContextMenu(hwnd)
		return 0

	case wmTimer:
		if wParam == timerJiggle {
			jiggleMouse()
		} else if wParam == timerAnimation {
			advanceFrame(hwnd)
		}
		return 0

	case wmDestroy:
		saveLastPosition()
		for _, bmp := range frameBitmaps {
			if bmp != 0 {
				pDeleteObject.Call(bmp)
			}
		}
		pPostQuitMessage.Call(0)
		return 0
	}

	ret, _, _ := pDefWindowProcW.Call(hwnd, uMsg, wParam, lParam)
	return ret
}

func onPaint(hwnd uintptr) uintptr {
	var ps paintStruct
	hdc, _, _ := pBeginPaint.Call(hwnd, uintptr(unsafe.Pointer(&ps)))

	// Frames are pre-scaled to windowSize — use fast BitBlt (no stretching)
	memDC, _, _ := pCreateCompatibleDC.Call(hdc)
	oldBmp, _, _ := pSelectObject.Call(memDC, hBitmap)

	pBitBlt.Call(
		hdc, 0, 0, uintptr(windowSize), uintptr(windowSize),
		memDC, 0, 0,
		srcCopy,
	)

	pSelectObject.Call(memDC, oldBmp)
	pDeleteDC.Call(memDC)
	pEndPaint.Call(hwnd, uintptr(unsafe.Pointer(&ps)))
	return 0
}

// ============================================================
// Animation
// ============================================================

func advanceFrame(hwnd uintptr) {
	currentFrame = (currentFrame + 1) % len(frameBitmaps)
	hBitmap = frameBitmaps[currentFrame]

	// Re-set the timer with the new frame's delay
	pKillTimer.Call(hwnd, timerAnimation)
	pSetTimer.Call(hwnd, timerAnimation, uintptr(frameDelays[currentFrame]), 0)

	// Trigger repaint without erasing background (no flicker)
	pInvalidateRect.Call(hwnd, 0, 0)
}

// ============================================================
// Context menu
// ============================================================

func showContextMenu(hwnd uintptr) {
	var pt point
	pGetCursorPos.Call(uintptr(unsafe.Pointer(&pt)))

	hMenu, _, _ := pCreatePopupMenu.Call()

	startText, _ := syscall.UTF16PtrFromString("Start with Windows")
	exitText, _ := syscall.UTF16PtrFromString("Exit")

	flags := uintptr(mfString)
	if startWithWindows {
		flags |= mfChecked
	}
	pAppendMenuW.Call(hMenu, flags, idmStartWithWindows, uintptr(unsafe.Pointer(startText)))
	pAppendMenuW.Call(hMenu, mfString, idmExit, uintptr(unsafe.Pointer(exitText)))

	pSetForegroundWindow.Call(hwnd)

	cmd, _, _ := pTrackPopupMenu.Call(
		hMenu,
		tpmReturnCmd|tpmNoNotify|tpmLeftAlign,
		uintptr(pt.X), uintptr(pt.Y),
		0, hwnd, 0,
	)
	pDestroyMenu.Call(hMenu)

	switch cmd {
	case idmStartWithWindows:
		toggleStartWithWindows()
	case idmExit:
		pDestroyWindow.Call(hwnd)
	}
}

// ============================================================
// Mouse jiggle (prevents screen lock / sleep)
// ============================================================

func jiggleMouse() {
	n := rand.Intn(4) + 2 // random 2..5
	pMouseEvent.Call(mouseeventfMove, uintptr(n), uintptr(n), 0, 0)
	time.Sleep(10 * time.Millisecond)
	neg := uintptr(uint32(int32(-n)))
	pMouseEvent.Call(mouseeventfMove, neg, neg, 0, 0)
}

// ============================================================
// Registry: Start with Windows
// ============================================================

func checkStartWithWindows() bool {
	k, err := registry.OpenKey(registry.CURRENT_USER, regKeyPath, registry.QUERY_VALUE)
	if err != nil {
		return false
	}
	defer k.Close()
	_, _, err = k.GetStringValue(regValueName)
	return err == nil
}

func toggleStartWithWindows() {
	startWithWindows = !startWithWindows
	k, err := registry.OpenKey(registry.CURRENT_USER, regKeyPath, registry.SET_VALUE)
	if err != nil {
		return
	}
	defer k.Close()

	if startWithWindows {
		if exe, err := os.Executable(); err == nil {
			k.SetStringValue(regValueName, exe)
		}
	} else {
		k.DeleteValue(regValueName)
	}
}

// ============================================================
// Position save / load
// ============================================================

func getLastLocation() (int32, int32) {
	p := filepath.Join(os.TempDir(), "hologram_go_location.txt")
	data, err := os.ReadFile(p)
	if err != nil {
		return 1800, 900
	}
	lines := strings.Split(strings.TrimSpace(string(data)), "\n")
	if len(lines) >= 2 {
		x, e1 := strconv.Atoi(strings.TrimSpace(lines[0]))
		y, e2 := strconv.Atoi(strings.TrimSpace(lines[1]))
		if e1 == nil && e2 == nil {
			return int32(x), int32(y)
		}
	}
	return 1800, 900
}

func saveLastPosition() {
	var r rect
	pGetWindowRect.Call(mainHwnd, uintptr(unsafe.Pointer(&r)))
	p := filepath.Join(os.TempDir(), "hologram_go_location.txt")
	data := fmt.Sprintf("%d\n%d", r.Left, r.Top)
	os.WriteFile(p, []byte(data), 0644)
}

// ============================================================
// Main
// ============================================================

func main() {
	runtime.LockOSThread()

	loadImage()

	hInstance, _, _ := pGetModuleHandleW.Call(0)

	className, _ := syscall.UTF16PtrFromString("HologramGoClass")
	cursor, _, _ := pLoadCursorW.Call(0, idcArrow)
	bgBrush, _, _ := pCreateSolidBrush.Call(colorLimeGreen)

	wc := wndClassExW{
		CbSize:        uint32(unsafe.Sizeof(wndClassExW{})),
		Style:         csHRedraw | csVRedraw,
		LpfnWndProc:   syscall.NewCallback(wndProc),
		HInstance:     hInstance,
		HCursor:       cursor,
		HbrBackground: bgBrush,
		LpszClassName: className,
	}
	pRegisterClassExW.Call(uintptr(unsafe.Pointer(&wc)))

	posX, posY := getLastLocation()
	windowName, _ := syscall.UTF16PtrFromString("Hologram")

	hwnd, _, _ := pCreateWindowExW.Call(
		wsExLayered|wsExTopmost|wsExToolwindow,
		uintptr(unsafe.Pointer(className)),
		uintptr(unsafe.Pointer(windowName)),
		wsPopup|wsVisible,
		uintptr(posX), uintptr(posY),
		uintptr(windowSize), uintptr(windowSize),
		0, 0, hInstance, 0,
	)
	mainHwnd = hwnd

	// LimeGreen pixels become transparent
	pSetLayeredWindowAttributes.Call(hwnd, colorLimeGreen, 0, lwaColorKey)

	// Always on top
	pSetWindowPos.Call(hwnd, hwndTopmost, 0, 0, 0, 0, swpNoMove|swpNoSize)

	// Mouse jiggle timer: every 2 minutes (120 000 ms)
	pSetTimer.Call(hwnd, timerJiggle, 120000, 0)

	// Animation timer: start with first frame's delay
	if len(frameBitmaps) > 1 {
		pSetTimer.Call(hwnd, timerAnimation, uintptr(frameDelays[0]), 0)
	}

	startWithWindows = checkStartWithWindows()

	pShowWindow.Call(hwnd, swShow)
	pUpdateWindow.Call(hwnd)

	// Message loop
	var m msg
	for {
		ret, _, _ := pGetMessageW.Call(uintptr(unsafe.Pointer(&m)), 0, 0, 0)
		if ret == 0 || ret == ^uintptr(0) {
			break
		}
		pTranslateMessage.Call(uintptr(unsafe.Pointer(&m)))
		pDispatchMessageW.Call(uintptr(unsafe.Pointer(&m)))
	}
}
