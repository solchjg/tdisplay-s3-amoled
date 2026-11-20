package WaterMonitor::Config;

# Reads the same water.toml as the Python version. Only the TOML this file
# uses: [table], [[array-of-tables]], key = "string" | number | true/false |
# ["list", "of", "strings"], and # comments. Secrets come from the
# environment variables the file names.

use strict;
use warnings;
use File::Basename qw(dirname);
use File::Spec;
use Cwd qw(abs_path);
use WaterMonitor::Report qw(parse_offset);
use Exporter 'import';

our @EXPORT_OK = qw(load_config parse_toml);

sub _value {
    my ($v) = @_;
    return $1 =~ s/\\"/"/gr if $v =~ /^"((?:[^"\\]|\\.)*)"$/;
    return $1 if $v =~ /^'([^']*)'$/;
    return 1 if $v eq 'true';
    return 0 if $v eq 'false';
    if ($v =~ /^\[(.*)\]$/) {
        my $in = $1;
        return [map { _value($_) } ($in =~ /("(?:[^"\\]|\\.)*"|'[^']*'|[^,\s]+)/g)];
    }
    return $v + 0 if $v =~ /^[+-]?\d+(\.\d+)?$/;
    die "toml: cannot read value $v\n";
}

sub parse_toml {
    my ($text) = @_;
    my %root;
    my $cur = \%root;
    for my $line (split /\n/, $text) {
        # Drop a comment that is not inside a string.
        $line =~ s/^((?:[^"'#]|"(?:[^"\\]|\\.)*"|'[^']*')*)#.*$/$1/;
        $line =~ s/^\s+|\s+$//g;
        next unless length $line;
        if ($line =~ /^\[\[\s*([\w.-]+)\s*\]\]$/) {
            push @{ $root{$1} }, {};
            $cur = $root{$1}[-1];
        } elsif ($line =~ /^\[\s*([\w.-]+)\s*\]$/) {
            $cur = $root{$1} //= {};
        } elsif ($line =~ /^([\w-]+)\s*=\s*(.+)$/) {
            $cur->{$1} = _value($2);
        } else {
            die "toml: cannot read line: $line\n";
        }
    }
    return \%root;
}

sub load_config {
    my ($path) = @_;
    open my $fh, '<:encoding(UTF-8)', $path or die "cannot read $path: $!\n";
    my $raw = parse_toml(do { local $/; <$fh> });
    my $base = dirname(abs_path($path));
    my $rel = sub { File::Spec->rel2abs($_[0], $base) };
    my %c = (
        store      => $rel->($raw->{store}{path} // 'water.sqlite'),
        export_dir => $rel->($raw->{export}{dir} // 'exports'),
        keep_days  => $raw->{export}{keep_days} // 90,
        late_hours => $raw->{alerts}{late_hours} // 26,
        battery_low_pct => $raw->{alerts}{battery_low_pct} // 30,
        mail  => {port => 993, password_env => 'WM_IMAP_PASSWORD',
                  folder => 'INBOX', done_folder => 'done',
                  failed_folder => 'failed', %{ $raw->{mail} // {} }},
        email => {smtp_port => 587, security => 'starttls',
                  password_env => 'WM_SMTP_PASSWORD', to => [],
                  subject => 'Water monitoring report',
                  %{ $raw->{email} // {} }},
        sites => {}, site_order => [],
    );
    for my $s (@{ $raw->{site} // [] }) {
        my $serial = "$s->{serial}";
        $c{sites}{$serial} = {serial => $serial, name => $s->{name} // '',
                              offset => parse_offset("$s->{utc_offset}")};
        push @{ $c{site_order} }, $serial;
    }
    return \%c;
}

1;
