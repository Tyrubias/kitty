package choose_files

import (
	"fmt"
	"path/filepath"
	"strings"

	"github.com/kovidgoyal/kitty/tools/utils"
	"github.com/kovidgoyal/kitty/tools/utils/style"
	"github.com/kovidgoyal/kitty/tools/wcswidth"
)

var _ = fmt.Print

const HOVER_STYLE = "default fg=red"

type single_line_region struct {
	x, width, y int
	id          string
	callback    func(string) error
}

func (h *Handler) draw_footer() (num_lines int, err error) {
	lines := []string{}
	screen_width := h.screen_size.width
	sctx := style.Context{AllowEscapeCodes: true}
	if len(h.state.filter_map)+len(h.state.selections) > 0 {
		buf := strings.Builder{}
		pos := 0
		current_style := sctx.SprintFunc("italic fg=green intense")
		non_current_style := sctx.SprintFunc("dim")
		var crs []single_line_region
		w := func(presep, text string, sfunc func(...any) string, id string, cb func(string)) {
			sz := len(presep)
			if sz+pos >= screen_width {
				lines = append(lines, buf.String())
				pos = 0
				buf.Reset()
			}
			buf.WriteString(presep)
			pos += sz
			sz = wcswidth.Stringwidth(text)
			if sfunc != nil {
				text = sfunc(text)
			}
			buf.WriteString(text)
			if cb != nil {
				crs = append(crs, single_line_region{x: pos, width: sz - 1, y: len(lines), id: id, callback: func(filter string) error {
					cb(filter)
					h.state.redraw_needed = true
					return nil
				}})
			}
			pos += sz
		}
		flush := func() {
			if s := buf.String(); s != "" {
				lines = append(lines, s)
			}
			pos = 0
			buf.Reset()
		}
		if len(h.state.filter_map) > 0 {
			w("", "󰈲  Filter:", nil, "", nil)
			for _, name := range h.state.filter_names {
				var cb func(string)
				if name != h.state.current_filter {
					cb = func(filter string) { h.set_filter(filter) }
				}
				w("  ", name, utils.IfElse(name == h.state.current_filter, current_style, non_current_style), name, cb)
			}
			flush()
		}
		if len(h.state.selections) > 0 {
			w("", "  Selected:", nil, "", nil)
			for _, s := range h.state.selections {
				text := s
				if rel, rerr := filepath.Rel(h.state.CurrentDir(), s); rerr == nil {
					text = rel
				}
				w("  ", text, nil, s, func(abspath string) { h.state.ToggleSelection(abspath) })
			}
			flush()
		}
		offset := h.screen_size.height - len(lines)
		for _, cr := range crs {
			h.state.mouse_state.AddCellRegion(cr.id, cr.x, cr.y+offset, cr.x+cr.width, cr.y+offset, cr.callback).HoverStyle = HOVER_STYLE
		}
	}
	if len(lines) > 0 {
		h.lp.MoveCursorTo(1, h.screen_size.height-len(lines)+1)
		h.lp.QueueWriteString(strings.Join(lines, "\r\n"))
	}
	return len(lines), err
}
