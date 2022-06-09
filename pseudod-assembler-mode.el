;;; pseudod-assembler-mode.el --- Summary.

;;; Commentary:

;;; Code:

(defgroup pseudod-assembler nil
  "PseudoD's assembly language support for Emacs."
  :group 'languages)

(defcustom pseudod-assembler-indentation-level 2
  "Indentation level in pdasm buffers.

Basically, the number of spaces that each step will be."
  :type 'integer
  :group 'pseudod-assembler)

;; Identation:

;; The identation algorithm is very simple, when text gets indented we
;; need to:
;;
;; 1. Indent to the same level than the line before.
;; 2. Deindent if this line begins with `END' or is a closing parenthesis.
;; 3. Indent more if the previous line contains `PROC', `SECTION'.
;;
;; Right now, that indentation algorithm is not implemented, instead TAB always
;; indents.

(defun pseudod-assembler--indentation-of-current-line (&optional pt)
  "Obtain the indentation of the current line.

If PT is provided, the current line is calculated with respect to
PT and not the current point."
  (let ((cur (or pt (point)))
        line-start
        first-nonspace-char-pos
        line-end)
    (save-mark-and-excursion
      (goto-char cur)
      (end-of-line)
      (setq line-end (point))
      (beginning-of-line)
      (setq line-start (point))
      (setq first-nonspace-char-pos
            (re-search-forward "[^ \t\n]" line-end 'no-error))
      (if (not first-nonspace-char-pos)
          0
        (- (1- first-nonspace-char-pos) line-start)))))

(defun pseudod-assembler--get-current-line (&optional pt)
  (save-mark-and-excursion
    (beginning-of-line)
    (let ((bol (point)))
      (end-of-line)
      (let ((eol (point)))
        (string-trim-left (buffer-substring-no-properties bol eol))))))

(defun pseudod-assembler--get-previous-line (&optional pt)
  (save-mark-and-excursion
    (if (= (forward-line -1) -1)
        nil
      (pseudod-assembler--get-current-line))))

(defun pseudod-assembler--indentation-of-previous-line (&optional pt)
  "Obtain the indentation of the previous line.

If the previous line contains only whitespace or is empty then it's
ignored and the previous-previous one is used instead.

PT is the point from which the previous line will be calculated.  If
not specified then the current point will be used instead.

See also `pseudod-assembler--indentation-of-current-line'."
  (let ((cur (or pt (point))))
    (save-mark-and-excursion
      (goto-char cur)
      (beginning-of-line)
      (forward-line -1)
      (while (and (string-empty-p
                   (string-trim
                    (pseudod-assembler--get-current-line)))
                  (not (bobp)))
        (forward-line -1)
        (beginning-of-line))
      (pseudod-assembler--indentation-of-current-line))))

(defun pseudod-assembler-indent-line-to-previous ()
  "Indents the current line to the indentation of the previous line."
  (interactive)
  (save-mark-and-excursion
    (let ((indentation (pseudod-assembler--indentation-of-previous-line)))
      (indent-line-to indentation))))

(defun pseudod-assembler-indent ()
  "Indent the current function to the next tab stop."
  (interactive)
  (save-mark-and-excursion
    (let ((indentation (pseudod-assembler--indentation-of-current-line))
          (code (string-trim-left (pseudod-assembler--get-current-line))))
      (cond ((or (string-prefix-p "PROC" code)
                 (string-prefix-p "ENDPROC" code))
             (indent-line-to pseudod-assembler-indentation-level))
            ((or (string-prefix-p "ENDSECTION" code)
                 (string-prefix-p "SECTION" code))
             (indent-line-to 0))
            (t
             (let ((previous-line-indent (pseudod-assembler--indentation-of-previous-line))
                   (previous-line (pseudod-assembler--get-previous-line)))
               (cond ((not previous-line) (indent-line-to 0))
                     ((or (string-prefix-p "PROC" previous-line)
                          (string-prefix-p "SECTION" previous-line))
                      (indent-line-to (+ previous-line-indent
                                         pseudod-assembler-indentation-level)))
                     (t (pseudod-assembler-indent-line-to-previous)))))))))

(defun pseudod-assembler-indent-region (start end)
  "Indent all lines in a region to the next tab stop.

Works on the region between the POINT and the MARK."
  (interactive "r")
  (save-mark-and-excursion
    (goto-char start)
    (beginning-of-line)
    (while (< (point) end)
      (pseudod-assembler-indent)
      (forward-line 1)
      (beginning-of-line))))

(defun pseudod-assembler-indent-function ()
  "Indent a line of pdasm code.

If this line was already indented to what would be it's default
indentation (or a bigger indentation) then extra tabs are added."
  (interactive)
  (if (= 0 (pseudod-assembler--indentation-of-current-line))
      (progn
        (pseudod-assembler-indent)
        (back-to-indentation))
    (pseudod-assembler-indent)))

;; Major Mode:

(defconst pseudod-assembler-font-lock-keywords
  '(("\\<\\(\\(END\\)?\\(SECTION\\|PROC\\)\\|LOCAL\\|PARAM\\|RETN\\|L[SG]ETC?\\|[IFLB]CONST\\|\\(OPN\\|CLS\\)FRM\\|E\\(INIT\\|NEW\\)\\|ROT\\|T?MSG\\|SUM\\|SUB\\|MUL\\|DIV\\|NAME\\|CHOOSE\\|[GL][ET]\\|JMP\\|DYNCALL\\|MTRUE\\|CMPN?EQ\\|NOT\\|MK0?\\(CLZ\\|OBJ\\|ARR\\)\\|VARIADIC\\|METHOD\\|PRN\\|NL\\|PDVM\\|PLATFORM\\|STRING\\|BIG\\(INT\\|DEC\\)\\|S?POP\\|SPUSH\\|OPEQ\\)\\>" . font-lock-keyword-face)
    ("\\([,#()]\\)" . font-lock-keyword-face))
  "Font locking for pdasm keywords.")

(defconst pseudod-assembler-font-lock-numbers
  '(("\\(-?[0-9]+\\(\\.[0-9]+\\)?\\)" . font-lock-constant-face))
  "Font locking for pdasm numbers.")

(defvar pseudod-assembler-font-lock-defaults
  `((,@pseudod-assembler-font-lock-keywords
     ,@pseudod-assembler-font-lock-numbers))
  "Font locking for pdasm.")

(defvar pseudod-assembler-mode-syntax-table
  (let ((st (make-syntax-table prog-mode-syntax-table)))
    (modify-syntax-entry ?- ". 12" st)
    (modify-syntax-entry ?\n ">" st)
    (modify-syntax-entry ?\" "\"" st)
    (modify-syntax-entry ?, "." st)
    (modify-syntax-entry ?_ "w" st)
    st)
  "Syntax table for pseudod-assembler-mode.")

;; Smartparens mode:

(defvar pseudod-assembler-mode-map
  (let ((map (make-sparse-keymap)))
    map)
  "Keymap for pseudod-assembler-mode.")

;;;###autoload
(define-derived-mode pseudod-assembler-mode prog-mode "pdasm"
  "Major mode for editing pdasm (PseudoD's assembler) source code.

\\<pseudod-assembler-mode-map>"
  :syntax-table pseudod-assembler-mode-syntax-table
  :group 'pseudod-assembler
  (setq-local font-lock-defaults pseudod-assembler-font-lock-defaults)
  (setq-local open-paren-in-column-0-is-defun-start nil)
  (setq-local indent-line-function #'pseudod-assembler-indent-function)
  (setq-local tab-width pseudod-assembler-indentation-level)
  (setq-local electric-indent-inhibit t)
  (setq-local comment-start "--")
  (setq-local comment-end "")
  (setq-local comment-padding " ")
  (setq-local comment-start-skip nil)
  (setq-local comment-use-syntax t))

;;;###autoload
(add-to-list 'auto-mode-alist
             '("\\.pdasm\\'" . pseudod-assembler-mode))

(provide 'pseudod-assembler)

;;; pseudod-assembler-mode.el ends here
