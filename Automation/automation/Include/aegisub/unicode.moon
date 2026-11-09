-- Copyright (c) 2007, Niels Martin Hansen, Rodrigo Braz Monteiro
-- All rights reserved.
--
-- Redistribution and use in source and binary forms, with or without
-- modification, are permitted provided that the following conditions are met:
--
--   * Redistributions of source code must retain the above copyright notice,
--     this list of conditions and the following disclaimer.
--   * Redistributions in binary form must reproduce the above copyright notice,
--     this list of conditions and the following disclaimer in the documentation
--     and/or other materials provided with the distribution.
--   * Neither the name of the Aegisub Group nor the names of its contributors
--     may be used to endorse or promote products derived from this software
--     without specific prior written permission.
--
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
-- POSSIBILITY OF SUCH DAMAGE.

-- Unicode (UTF-8) support functions for Aegisub Automation 4 Lua
-- http://www.ietf.org/rfc/rfc2279.txt

impl = require 'aegisub.__unicode_impl'

check = require 'aegisub.argcheck'
ffi = require 'ffi'
ffi_util = require 'aegisub.ffi'

err_buff = ffi.new 'char *[1]'
conv_func = (f) -> check'string' (str) ->
  err_buff[0] = nil
  result = f str, err_buff
  errmsg = ffi_util.string err_buff[0]
  if errmsg
    error errmsg, 2
  ffi_util.string result

joins_previous = (code) ->
  (code >= 0x0300 and code <= 0x036F) or (code >= 0x1AB0 and code <= 0x1AFF) or
  (code >= 0x1DC0 and code <= 0x1DFF) or (code >= 0x20D0 and code <= 0x20FF) or
  (code >= 0xFE20 and code <= 0xFE2F) or (code >= 0xFE00 and code <= 0xFE0F) or
  code == 0x0E31 or (code >= 0x0E33 and code <= 0x0E3A) or (code >= 0x0E47 and code <= 0x0E4E) or
  (code >= 0x1F3FB and code <= 0x1F3FF) or code == 0x200D

local unicode

native_grapheme_end = impl.grapheme_end
grapheme_end = if native_grapheme_end
  (s, i) -> native_grapheme_end s, #s, i - 1
else
  code_at = (s, i) ->
    last = i + unicode.charwidth(s, i) - 1
    return if last > #s
    unicode.codepoint s\sub i, last

  (s, i) ->
    previous = code_at s, i
    j = i + unicode.charwidth(s, i) - 1
    while j < #s
      code = code_at s, j + 1
      break unless code and (joins_previous(code) or previous == 0x200D)
      previous = code
      j += unicode.charwidth s, j + 1
    math.min j, #s

unicode =
  -- Return the number of bytes occupied by the character starting at the i'th byte in s
  charwidth: check'string ?number' (s, i) ->
    b = s\byte i or 1
    -- FIXME, something in karaskel results in this case, shouldn't happen
    -- What would "proper" behaviour be? Zero? Or just explode?
    if     not b   then 1
    elseif b < 128 then 1
    elseif b < 224 then 2
    elseif b < 240 then 3
    else                4

  -- Returns an iterator function for iterating over the characters in s
  chars: check'string' (s) ->
    curchar, i = 0, 1
    ->
      return if i > s\len()

      j = i
      curchar += 1
      i += unicode.charwidth s, i
      s\sub(j, i - 1), curchar

  -- Returns an iterator over the extended grapheme clusters (UAX #29) in s
  graphemes: check'string' (s) ->
    n, i = 0, 1
    ->
      return if i > s\len()

      j = grapheme_end s, i
      n += 1
      cluster = s\sub i, j
      i = j + 1
      cluster, n

  -- Returns the number of characters in s
  -- Runs in O(s:len()) time!
  len: check'string' (s) ->
    n = 0
    n += 1 for c in unicode.chars s
    n

  -- Get codepoint of first char in s
  codepoint: check'string' (s) ->
    -- Basic case, ASCII
    b = s\byte 1
    return b if b < 128

    -- Use a naive decoding algorithm, and assume input is valid
    local res, w

    if b < 224 then
      -- prefix byte is 110xxxxx
      res = b - 192
      w = 2
    elseif b < 240 then
      -- prefix byte is 11100000
      res = b - 224
      w = 3
    else
      res = b - 240
      w = 4

    for i = 2, w
      res = res*64 + s\byte(i) - 128
    res

  to_upper_case: conv_func impl.to_upper_case
  to_lower_case: conv_func impl.to_lower_case
  to_fold_case: conv_func impl.to_fold_case

return unicode
