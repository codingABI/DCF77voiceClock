# Create MP3 files for audio module
# All files will have two char file names from AA.mp3, AB.mp3, ..., ZZ.mp3
# You will need ffmpeg.exe (from https://ffmpeg.org/) to run this script

# Folder to store MP3 files (ZH-Folder is needed for DFR0534 combined audio files)
$TargetPath = (get-location).Path+'\ZH'
# Create ZH folder, if needed and delete all existing MP3 files
New-Item -Path $TargetPath -ItemType "directory" -ErrorAction SilentlyContinue
remove-item "$TargetPath\*.mp3" -ErrorAction SilentlyContinue
# Description text file for all created MP3 files
$DescriptionFile = "$TargetPath\_content.txt"
# Clear/delete existing descption file
remove-item $DescriptionFile -ErrorAction SilentlyContinue

# Folder for temporary created files
$TempPath = "$env:TEMP" 
# Recreate header file for defines
$IncludeFile = "$TargetPath\..\..\DCF77voiceClock\audioSamples.h"
remove-item $IncludeFile -ErrorAction SilentlyContinue
add-Content -Path $IncludeFile -Value "// Audio sample definitions"
add-Content -Path $IncludeFile -Value "#pragma once"
$i=0
# Number of chars per digit for filesnames (A to Z => 26 chars)
$chars = [byte][char]'Z'-[byte][char]'A'+1

# Create EN audio files
$TextSequenceFile = "$TargetPath\..\..\Languages\EN.txt"
Add-Content -Path $DescriptionFile -Encoding Unicode -Value "Language: EN"
Add-Type -AssemblyName System.Speech
$SpeechSynthesizer = New-Object -TypeName System.Speech.Synthesis.SpeechSynthesizer
# Select voice
$SpeechSynthesizer.SelectVoice('Microsoft Zira Desktop')
$streamFormat = [System.Speech.AudioFormat.SpeechAudioFormatInfo]::new(44100,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)

# Create audio file for every entry in "TextSequenceFile"
foreach($line in Get-Content $TextSequenceFile -encoding utf8) {
    $data = $line.Split("|")
	$Text = $data[1]
    $c1 = [byte]([Math]::Truncate([decimal]($i/$chars)))
    $c2 = [byte]($i%$chars)
    $Filename = "$([char]([byte][char]'A' + $c1))$([char]([byte][char]'A' + $c2))"
	$FilenameWAV = $Filename + ".wav"
	$FilenameMP3 = $Filename + ".mp3"
	# Set WAV file as speech output
	$SpeechSynthesizer.SetOutputToWaveFile("$TempPath\$FilenameWAV",$streamFormat)
	$SpeechSynthesizer.Speak($Text)
	..\internal\ffmpeg.exe -i "$TempPath\$FilenameWAV" -af silenceremove=start_periods=1:start_silence=0:start_threshold=-50dB,areverse,silenceremove=start_periods=1:start_silence=0.1:start_threshold=-50dB,areverse -y "$TargetPath\$FilenameMP3" 2>&1 | %{ "$_" }
	# Add define to header file
	Add-Content -Path $DescriptionFile -Encoding Unicode -Value "$FilenameMP3 = $Text"
	$comment = "`r`n// Voice: $Text"
	$define =  "#define " + $data[0] + " $i"
	add-Content -Path $IncludeFile -Value $comment
	add-Content -Path $IncludeFile -Value $define
	$i = $i + 1
}

# Finalize header file
$comment = "`r`n// Number of voice files"
$define =  "#define AUDIO_MAXFILES $i"
add-Content -Path $IncludeFile -Value $comment
add-Content -Path $IncludeFile -Value $define

# Create DE audio files
$TextSequenceFile = "$TargetPath\..\..\Languages\DE.txt"
Add-Content -Path $DescriptionFile -Encoding Unicode -Value "`r`nLanguage: DE"
Add-Type -AssemblyName System.Speech
$SpeechSynthesizer = New-Object -TypeName System.Speech.Synthesis.SpeechSynthesizer
# Select voice
$SpeechSynthesizer.SelectVoice('Microsoft Hedda Desktop')
$streamFormat = [System.Speech.AudioFormat.SpeechAudioFormatInfo]::new(44100,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)

# Create audio file for every entry in "TextSequenceFile"
foreach($line in Get-Content $TextSequenceFile -encoding utf8) {
    $data = $line.Split("|")
	$Text = $data[1]
    $c1 = [byte]([Math]::Truncate([decimal]($i/$chars)))
    $c2 = [byte]($i%$chars)
    $Filename = "$([char]([byte][char]'A' + $c1))$([char]([byte][char]'A' + $c2))"
    $FilenameWAV = $Filename + ".wav"
	$FilenameMP3 = $Filename + ".mp3"
	# Set WAV file as speech output
	$SpeechSynthesizer.SetOutputToWaveFile("$TempPath\$FilenameWAV",$streamFormat)
	$SpeechSynthesizer.Speak($Text)
	# Convert WAV to MP3 and shorten leading silence
	..\internal\ffmpeg.exe -i "$TempPath\$FilenameWAV" -af silenceremove=start_periods=1:start_silence=0:start_threshold=-50dB,areverse,silenceremove=start_periods=1:start_silence=0.1:start_threshold=-50dB,areverse -y "$TargetPath\$FilenameMP3" 2>&1 | %{ "$_" }
	remove-item "$TempPath\$FilenameWAV" -ErrorAction SilentlyContinue
	Add-Content -Path $DescriptionFile -Encoding Unicode -Value "$FilenameMP3 = $Text"
	$i = $i + 1
}

# Close SpeechSynthesizer and release last processed WAV file
$SpeechSynthesizer.dispose()

# Remove temporary created WAV files
for ($j = 0; $j -lt $i; $j++) {
    $c1 = [byte]([Math]::Truncate([decimal]($j/$chars)))
    $c2 = [byte]($j%$chars)
    $Filename = "$([char]([byte][char]'A' + $c1))$([char]([byte][char]'A' + $c2))"
    $FilenameWAV = $Filename + ".wav"
	remove-item "$TempPath\$FilenameWAV" -ErrorAction SilentlyContinue
}