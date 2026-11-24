# A window with ordinary controls for the GUI tests of controls (UI
# Automation).  Clicking Go writes the text box and check box to a file.
#
# Usage: powershell -File controls_target.ps1 <output file>

param([string]$Out)
Add-Type -AssemblyName System.Windows.Forms
$form = New-Object Windows.Forms.Form
$form.Text = "Controls Target"
$form.Width = 360
$form.Height = 200
$form.StartPosition = "Manual"
$form.Location = New-Object Drawing.Point(650, 100)

$label = New-Object Windows.Forms.Label
$label.Text = "Name:"
$label.Location = New-Object Drawing.Point(10, 14)
$label.AutoSize = $true
$box = New-Object Windows.Forms.TextBox
$box.Name = "NameBox"
$box.Location = New-Object Drawing.Point(70, 10)
$box.Width = 200
$check = New-Object Windows.Forms.CheckBox
$check.Name = "Agree"
$check.Text = "I agree"
$check.Location = New-Object Drawing.Point(70, 45)
$button = New-Object Windows.Forms.Button
$button.Name = "GoButton"
$button.Text = "Go"
$button.Location = New-Object Drawing.Point(70, 80)
$button.Add_Click({ Set-Content -Path $Out -Value ("clicked:" + $box.Text + ":" + $check.Checked) -Encoding UTF8 })
$form.Controls.AddRange(@($label, $box, $check, $button))
[Windows.Forms.Application]::Run($form)
