-- Original throwaway fixtures. This is not the application automation host.
local ffi = require 'ffi'
ffi.cdef 'unsigned long GetCurrentProcessId(void);'
local kernel = ffi.load('kernel32')
counter = 0
undo_stub_calls = 0
aegisub = {
  gettext = function(s) return s end,
  register_macro = function(name, description, fn) captured_macro = fn end,
  set_undo_point = function() undo_stub_calls = undo_stub_calls + 1 end,
  dialog = { display = host.dialog }
}

function runtime_identity()
  return {version=jit.version, version_num=jit.version_num, os=ffi.os,
    arch=ffi.arch, jit_enabled=jit.status(), ffi_pid=tonumber(kernel.GetCurrentProcessId())}
end

function run_fixture(mode)
  if mode == 'stuck' then while true do end end
  if mode == 'loss' then host.abrupt_exit() end
  if mode == 'error' then error('original fixture error') end
  if mode == 'cooperative' then
    for i=1,100000 do
      if host.is_cancelled() then error('fixture cooperative cancellation') end
      if i % 20 == 0 then host.progress(i) end
      host.sleep(2)
    end
    error('cooperative fixture unexpectedly exhausted')
  end
  if mode == 'edgeblur' then
    local records = {[1]={class='dialogue',text='Harbor'},[2]={class='dialogue',text='Unselected'}}
    local writes = 0
    local subs = setmetatable({}, {
      __index=function(_,i) local copy={} for k,v in pairs(records[i]) do copy[k]=v end return copy end,
      __newindex=function(_,i,v) records[i]=v; writes=writes+1 end
    })
    captured_macro(subs,{1},1)
    return {selected=records[1].text, unselected=records[2].text,
      writebacks=writes, undo_stub_calls=undo_stub_calls,
      limitation='Two-record proxy only; not full subtitle userdata or application undo.'}
  end
  counter = counter + 1
  io.stdout:write('LUA_STDOUT fixture ',mode,' ',counter,'\n'); io.stdout:flush()
  io.stderr:write('LUA_STDERR diagnostic, not protocol\n'); io.stderr:flush()
  local controls = {
    {class='label',label='Original synthetic dialog'},
    {class='edit',name='text',text='Harbor'},
    {class='intedit',name='count',value=7,min=0,max=99},
    {class='checkbox',name='enabled',label='Enabled',value=false},
    {class='dropdown',name='choice',items={'alpha','beta'},value='alpha'}
  }
  local buttons = (mode=='default_ok' or mode=='default_close') and {} or {'Apply','Cancel'}
  -- The third map is deliberately supplied and truncated by the native entry,
  -- matching the inspected current host entry instead of repairing C08.
  local button, values = aegisub.dialog.display(controls,buttons,{cancel='Cancel'})
  return {counter=counter, button=button, button_type=type(button),values=values,
    cancelled=host.is_cancelled(), runtime=runtime_identity()}
end
