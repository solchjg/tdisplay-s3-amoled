package WaterMonitor::Report;

# Parse one Solinst LevelSender email report (pure: text in, hash out).
# The Perl twin of water_monitor/report.py; the same rules, the same keys.
#
# The report may be the original email body, a forward (the LevelSender
# block under a "Forwarded Message" header block) or text from a PDF print.
# Everything from the line "LevelSender" on is parsed; the email's Subject
# and Date come from a forwarded header block when there is one.

use strict;
use warnings;
use Time::Local qw(timegm);
use Exporter 'import';

our @EXPORT_OK = qw(parse parse_offset to_utc_epoch rfc2822_epoch);

use constant NO_FIX => 999;

my %MON = (jan => 0, feb => 1, mar => 2, apr => 3, may => 4, jun => 5,
           jul => 6, aug => 7, sep => 8, oct => 9, nov => 10, dec => 11);

# Sample columns, matched by a word in the column title.
my @COLUMN_KEYS = (
    [temperature  => 'temp_f',     'temp_c'],
    [level        => 'level_ft',   'level_m'],
    [conductivity => 'cond_us_cm', 'cond_us_cm'],
    [pressure     => 'p_psi',      'p_kpa'],
);

# '+01:00', '-05:00', '+0530' or '0' as seconds.
sub parse_offset {
    my ($text) = @_;
    $text =~ /^\s*([+-])?(\d{1,2}):?(\d{2})?\s*$/
        or die "bad UTC offset '$text'\n";
    my $sign = ($1 // '+') eq '-' ? -1 : 1;
    return $sign * ($2 * 3600 + ($3 // 0) * 60);
}

# A logger clock time [y, m, d, H, M, S] (no zone) as UTC epoch seconds.
sub to_utc_epoch {
    my ($t, $offset) = @_;
    my ($y, $mo, $d, $h, $mi, $s) = @$t;
    return timegm($s, $mi, $h, $d, $mo - 1, $y) - $offset;
}

# 'Thu, 12 Nov 2026 19:25:19 +0000' as epoch seconds, or undef.
sub rfc2822_epoch {
    my ($text) = @_;
    return undef unless defined $text;
    $text =~ /(\d{1,2})\s+([A-Za-z]{3})\s+(\d{4})\s+(\d{1,2}):(\d{2})(?::(\d{2}))?\s*([+-]\d{4}|GMT|UT|Z)?/
        or return undef;
    my $mon = $MON{lc $2} // return undef;
    my $epoch = timegm($6 // 0, $5, $4, $1, $mon, $3);
    my $zone = $7 // '+0000';
    if ($zone =~ /^([+-])(\d{2})(\d{2})$/) {
        my $off = ($2 * 3600 + $3 * 60) * ($1 eq '-' ? -1 : 1);
        $epoch -= $off;
    }
    return $epoch;
}

sub _num {
    my ($text) = @_;
    return undef unless defined $text;
    return $text =~ /(-?\d+(?:\.\d+)?)/ ? $1 + 0 : undef;
}

# LevelSender dates are day first: 16/09/2026 20:23:22.
sub _date {
    my ($text) = @_;
    return undef unless defined $text;
    $text =~ m{(\d{2})/(\d{2})/(\d{4})\s+(\d{1,2}):(\d{2}):(\d{2})} or return undef;
    return [$3 + 0, $2 + 0, $1 + 0, $4 + 0, $5 + 0, $6 + 0];
}

sub _column_key {
    my ($title) = @_;
    my $low = lc $title;
    for my $c (@COLUMN_KEYS) {
        my ($word, $us, $si) = @$c;
        next unless index($low, $word) >= 0;
        my $metric = ($low =~ /\(\s?c\)|\(m\)|kpa/);
        return $metric ? $si : $us;
    }
    (my $slug = $low) =~ s/[^a-z0-9]+/_/g;
    $slug =~ s/^_+|_+$//g;
    return $slug;
}

# 'Name: value' lines as a hash with lower-case names. A PDF print can join
# two fields on one line ('State: reporting Start Report: 16/09/2026 ...').
sub _fields {
    my ($block) = @_;
    my %out;
    for my $line (split /\n/, $block) {
        $line =~ s/^\s+|\s+$//g;
        next unless index($line, ':') >= 0;
        if ($line =~ /^(State):\s*(.*?)\s+(Start Report):\s*(.*)$/) {
            $out{'state'} = $2;
            $out{'start report'} = $4;
            next;
        }
        my ($name, $value) = split /:/, $line, 2;
        $name = lc($name =~ s/^\s+|\s+$//gr);
        $value =~ s/^\s+|\s+$//g;
        $out{$name} //= $value;
    }
    return \%out;
}

sub _forwarded_header {
    my ($text, $name) = @_;
    return $text =~ /^\s*\Q$name\E:\s*(.+?)\s*$/m ? $1 : undef;
}

# Parse the report in $text. Dies with "report: <why>" if it is not one.
# Optional: subject => ..., email_epoch => ... from the message headers.
sub parse {
    my ($text, %opt) = @_;
    $text =~ s/\r\n/\n/g;
    $text =~ s/\x{fb00}/ff/g;
    $text =~ /^[ \t]*LevelSender[ \t]*\n[ \t]*Serial:/m
        or die "report: no LevelSender block\n";
    my $head = substr($text, 0, $-[0]);
    my $body = substr($text, $-[0]);

    my $subject = $opt{subject} // _forwarded_header($head, 'Subject') // '';
    my $email_epoch = $opt{email_epoch}
        // rfc2822_epoch(_forwarded_header($head, 'Date'));

    $body =~ /^[ \t]*Logger[ \t]+\d+[ \t]+Samples[ \t]*$/m
        or die "report: no samples section\n";
    my $meta  = substr($body, 0, $-[0]);
    my $table = substr($body, $+[0]);
    my $messages = '';
    if ($table =~ /^[ \t]*MESSAGES:[ \t]*(.*)$/m) {
        $messages = $1;
        $messages =~ s/\s+$//;
        $table = substr($table, 0, $-[0]);
    }

    my @parts = split /^[ \t]*Logger[ \t]+(\d+)[ \t]*$/m, $meta;
    my $header = _fields(shift @parts);
    my @loggers;
    while (@parts >= 2) {
        my ($index, $block) = splice @parts, 0, 2;
        my $f = _fields($block);
        push @loggers, {index => $index + 0, fields => $f} if %$f;  # NA: none
    }

    my @samples = _samples($table);
    die "report: no sample rows\n" unless @samples;
    my $serial = $header->{serial} // '';
    $serial =~ s/^\s+|\s+$//g;
    die "report: no serial\n" unless length $serial;

    my $report_no;
    for my $src ($messages, $subject) {
        if ($src =~ /report\s+(\d+)/i) { $report_no = $1 + 0; last }
    }
    my ($lat, $lon) = (_num($header->{latitude}), _num($header->{longitude}));
    ($lat, $lon) = (undef, undef)
        if (defined $lat && $lat == NO_FIX) || (defined $lon && $lon == NO_FIX);
    my $signal = _num($header->{'signal strength'});

    return {
        serial       => $serial,
        location     => $header->{location} // '',
        report_no    => $report_no,
        battery_pct  => _num($header->{battery}),
        bat_v        => _num($header->{batvolt}),
        usb_v        => _num($header->{usbvolt}),
        signal       => defined $signal ? int($signal) : undef,
        state        => $header->{state} // '',
        start_report => _date($header->{'start report'}),
        lat          => $lat,
        lon          => $lon,
        loggers      => \@loggers,
        samples      => \@samples,
        messages     => $messages,
        email_epoch  => $email_epoch,
        subject      => $subject,
        fields       => $header,
    };
}

# A field of logger 1 (or $index), e.g. logger_field($r, 'state').
sub logger_field {
    my ($r, $name, $index) = @_;
    $index //= 1;
    for my $lg (@{ $r->{loggers} }) {
        return $lg->{fields}{$name} if $lg->{index} == $index;
    }
    return undef;
}

sub _samples {
    my ($table) = @_;
    my ($keys, @rows);
    for my $line (split /\n/, $table) {
        $line =~ s/^\s+|\s+$//g;
        next unless length $line;
        my @cells = map { s/^\s+|\s+$//gr } split /,/, $line, -1;
        if (!$keys) {
            $keys = [map { _column_key($_) } @cells[1 .. $#cells]]
                if lc($cells[0]) =~ /^time/;
            next;
        }
        my $when = _date($cells[0]) or next;
        my %values;
        for my $i (0 .. $#$keys) {
            my $v = _num($cells[$i + 1]);
            $values{ $keys->[$i] } = $v if defined $v;
        }
        push @rows, {local_time => $when, values => \%values};
    }
    return @rows;
}

1;
