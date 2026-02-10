#
# Mini-service webcontent Makefile
#

CONTENT = index.h

EXTRACLEAN = $(CONTENT) converttool app.js index.html
INDEX_H_MAX_BYTES ?= 98304

UI_TEMPLATE = index.template.html
UI_STYLE = style.css
UI_APP = app.js
UI_BUILDER = build-index.sh
UI_APP_PARTS = \
	app/00-wrap-start.js \
	app/10-state.js \
	app/20-status-layout.js \
	app/30-utils.js \
	app/40-actions-modal.js \
	app/50-list-render.js \
	app/60-render.js \
	app/70-queue-zip.js \
	app/80-upload-poll.js \
	app/90-events-boot.js \
	app/99-wrap-end.js

all: converttool $(CONTENT)

$(UI_APP): $(UI_APP_PARTS)
	@echo "  GEN   $@"
	@{ \
		echo '// generated from app/*.js - do not edit app.js directly'; \
		cat $(UI_APP_PARTS); \
	} > $(UI_APP)

index.html: $(UI_TEMPLATE) $(UI_STYLE) $(UI_APP) $(UI_BUILDER)
	@echo "  GEN   $@"
	@./$(UI_BUILDER) "$(UI_TEMPLATE)" "$(UI_STYLE)" "$(UI_APP)" > $@

index.h: index.html converttool
	@echo "  GEN   $@"
	@./converttool $< > $@
	@size=$$(wc -c < "$@"); \
	if [ "$$size" -gt "$(INDEX_H_MAX_BYTES)" ]; then \
		echo "ERROR: $@ $$size bytes exceeds budget $(INDEX_H_MAX_BYTES) bytes"; \
		rm -f "$@"; \
		exit 1; \
	fi

converttool: ../converttool.c
	@echo "  TOOL  $@"
	@gcc -o $@ $<

clean:
	rm -f $(EXTRACLEAN)
